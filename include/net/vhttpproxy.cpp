#include <VHttpProxy>
#include <VDebugNew>

// ----------------------------------------------------------------------------
// VHttpProxyOutPolicy
// ---------------------------------------------------------------------------
VHttpProxyOutPolicy::VHttpProxyOutPolicy()
{
  method = Auto;
  host   = "";
  port   = 0;
}

void VHttpProxyOutPolicy::load(VXml xml)
{
  method = (Method)xml.getInt("method", (int)method);
  host   = xml.getStr("host", host);
  port   = xml.getInt("port", port);
}

void VHttpProxyOutPolicy::save(VXml xml)
{
  xml.setInt("method", (int)method);
  xml.setStr("host", host);
  xml.setInt("port", port);
}

#ifdef QT_GUI_LIB
void VHttpProxyOutPolicy::optionAddWidget(QLayout* layout)
{
  QStringList sl; sl << "Auto" << "TCP" << "SSL";
  VOptionable::addComboBox(layout, "cbxMethod", "Method", sl, (int)method);
  VOptionable::addLineEdit(layout, "leHost", "Host", host);
  VOptionable::addLineEdit(layout, "lePort", "Port", QString::number(port));
}

void VHttpProxyOutPolicy::optionSaveDlg(QDialog* dialog)
{
  method = (Method)(dialog->findChild<QComboBox*>("cbxMethod")->currentIndex());
  host   = dialog->findChild<QLineEdit*>("leHost")->text();
  port   = dialog->findChild<QLineEdit*>("lePort")->text().toInt();
}
#endif // QT_GUI_LIB

// ----------------------------------------------------------------------------
// VHttpProxyOutInThread
// ----------------------------------------------------------------------------
class VHttpProxyOutInThread : public VThread
{
protected:
  VNetClient*  outClient;
  VNetSession* inSession;

public:
  VHttpProxyOutInThread(VNetClient* outClient, VNetSession* inSession, void* owner) : VThread(owner)
  {
    this->outClient = outClient;
    this->inSession = inSession;
  }
  virtual ~VHttpProxyOutInThread()
  {
    close();
  }

protected:
  virtual void run()
  {
    VHttpProxy* proxy = (VHttpProxy*)owner;
    VNetSession* outSession;
    {
      VTcpClient* tcpClient = dynamic_cast<VTcpClient*>(outClient);
      if (tcpClient != NULL) outSession = tcpClient->tcpSession;

      VSslClient* sslClient = dynamic_cast<VSslClient*>(outClient);
      if (sslClient != NULL) outSession = sslClient->tcpSession;

      if (outSession == NULL)
      {
        LOG_FATAL("outSession is NULL");
        return;
      }
    }
    LOG_DEBUG("stt"); // gilgil temp 2013.10.19
    while (true)
    {
      QByteArray msg;
      int readLen = outClient->read(msg);
      if (readLen == VERR_FAIL) break;
      proxy->inboundDataChange.change(msg, NULL);
      emit proxy->beforeMsg(msg, outSession);
      emit proxy->beforeResponse(msg, outClient, inSession);
      int writeLen = inSession->write(msg);
      if (writeLen == VERR_FAIL) break;
     }
    inSession->close();
    outClient->close();
    LOG_DEBUG("end"); // gilgil temp 2013.10.19
  }
};

// ----------------------------------------------------------------------------
// VHttpProxy
// ----------------------------------------------------------------------------
VHttpProxy::VHttpProxy(void* owner) : VObject(owner)
{
  tcpEnabled     = true;
  sslEnabled     = true;

  tcpServer.port = HTTP_PROXY_PORT;
  sslServer.port = SSL_PROXY_PORT;

  VObject::connect(&tcpServer, SIGNAL(runned(VTcpSession*)), this, SLOT(tcpRun(VTcpSession*)), Qt::DirectConnection);
  VObject::connect(&sslServer, SIGNAL(runned(VSslSession*)), this, SLOT(sslRun(VSslSession*)), Qt::DirectConnection);
}

VHttpProxy::~VHttpProxy()
{
  close();
}

bool VHttpProxy::doOpen()
{
  if (tcpEnabled)
  {
    if (!tcpServer.open())
    {
      error = tcpServer.error;
      return false;
    }
  }

  if (sslEnabled)
  {
    if (!sslServer.open())
    {
      error = sslServer.error;
      return false;
    }
  }

  if (!inboundDataChange.prepare(error)) return false;
  if (!outboundDataChange.prepare(error)) return false;

  return true;
}

bool VHttpProxy::doClose()
{
  tcpServer.close();
  sslServer.close();

  return true;
}

void VHttpProxy::tcpRun(VTcpSession* tcpSession)
{
  run(tcpSession);
}

void VHttpProxy::sslRun(VSslSession* sslSession)
{
  run(sslSession);
}

void VHttpProxy::run(VNetSession* inSession)
{
  LOG_DEBUG("stt inSession=%p", inSession); // gilgil temp 2013.10.19

  VNetClient* outClient = NULL;
  int defaultOutPort;
  switch (outPolicy.method)
  {
    case VHttpProxyOutPolicy::Auto:
      if (dynamic_cast<VTcpSession*>(inSession) != NULL)
      {
        outClient      = new VTcpClient;
        defaultOutPort = DEFAULT_HTTP_PORT;
      } else
      if (dynamic_cast<VSslSession*>(inSession) != NULL)
      {
        outClient      = new VSslClient;
        defaultOutPort = DEFAULT_SSL_PORT;
      } else
      {
        LOG_FATAL("invalid inSession type(%s)", qPrintable(inSession->className()));
        return;
      }
      break;
    case VHttpProxyOutPolicy::Tcp:
      outClient      = new VTcpClient;
      defaultOutPort = DEFAULT_HTTP_PORT;
      break;
    case VHttpProxyOutPolicy::Ssl:
      outClient      = new VSslClient;
      defaultOutPort = DEFAULT_SSL_PORT;
      break;
    default:
      LOG_FATAL("invalid method value(%d)", (int)outPolicy.method);
      return;
  }

  VHttpRequest           request;
  VHttpProxyOutInThread* thread = NULL;

  QByteArray totalMsg;
  while (true)
  {
    QByteArray msg;
    int readLen = inSession->read(msg);
    if (readLen == VERR_FAIL) break;
    outboundDataChange.change(msg, NULL);
    emit beforeMsg(msg, inSession);
    // LOG_DEBUG("%s", packet.data()); // gilgil temp

    totalMsg += msg;
    if (!request.parse(totalMsg))
    {
      if (outClient->active())
      {
        outClient->write(totalMsg);
        totalMsg = "";
      }
      continue;
    }

    QString host;
    int port;
    QUrl url = request.requestLine.path;
    if (!url.isRelative())
    {
      host = url.host();
      port = url.port();

      QByteArray newPath = url.path().toUtf8();
      if (url.hasQuery())
        newPath += "?" + url.query(QUrl::FullyEncoded).toLatin1();
      request.requestLine.path = newPath;
    } else
    if (!request.findHost(host, port))
    {
      LOG_ERROR("can not find host:%s", totalMsg.data());
      break;
    }
    if (port == -1) port = defaultOutPort;
    if (outPolicy.host != "") host = outPolicy.host;
    if (outPolicy.port != 0)  port = outPolicy.port;

    if (outClient->host != host || outClient->port != port)
    {
      outClient->close();
      if (thread != NULL) delete thread;

      outClient->host = host;
      outClient->port = port;
      LOG_DEBUG("opening %s:%d", qPrintable(host), port);
      if (!outClient->open())
      {
        LOG_ERROR("%s", outClient->error.msg);
        break;
      }
      thread = new VHttpProxyOutInThread(outClient, inSession, this);
      thread->open();
    }

    emit beforeRequest(request, inSession, outClient);
    outClient->write(request.toByteArray());
    totalMsg = "";
  }
  LOG_DEBUG("end inSession=%p", inSession); // gilgil temp 2013.10.19
  outClient->close();
  if (thread != NULL) delete thread;
  delete outClient;
}

void VHttpProxy::load(VXml xml)
{
  tcpEnabled = xml.getBool("tcpEnabled", tcpEnabled);
  sslEnabled = xml.getBool("sslEnabled", sslEnabled);
  outPolicy.load(xml.gotoChild("outPolicy"));
  tcpServer.load(xml.gotoChild("tcpServer"));
  sslServer.load(xml.gotoChild("sslServer"));
  inboundDataChange.load(xml.gotoChild("inboundDataChange"));
  outboundDataChange.load(xml.gotoChild("outboundDataChange"));
}

void VHttpProxy::save(VXml xml)
{
  xml.setBool("tcpEnabled", tcpEnabled);
  xml.setBool("sslEnabled", sslEnabled);
  outPolicy.save(xml.gotoChild("outPolicy"));
  tcpServer.save(xml.gotoChild("tcpServer"));
  sslServer.save(xml.gotoChild("sslServer"));
  inboundDataChange.save(xml.gotoChild("inboundDataChange"));
  outboundDataChange.save(xml.gotoChild("outboundDataChange"));
}

#ifdef QT_GUI_LIB
#include "vhttpproxywidget.h"
#include "ui_vhttpproxywidget.h"
void VHttpProxy::optionAddWidget(QLayout* layout)
{
  VHttpProxyWidget* widget = new VHttpProxyWidget(layout->parentWidget());
  widget->setObjectName("httpProcyWidget");

  VOptionable::addCheckBox(widget->ui->glTcpServer, "chkTcpEnabled", "TCP Enabled", tcpEnabled);
  VOptionable::addCheckBox(widget->ui->glSslServer, "chkSslEnabled", "SSL Enabled", sslEnabled);

  outPolicy.optionAddWidget(widget->ui->glExternal);
  tcpServer.optionAddWidget(widget->ui->glTcpServer);
  sslServer.optionAddWidget(widget->ui->glSslServer);
  inboundDataChange.optionAddWidget(widget->ui->glInbound);
  outboundDataChange.optionAddWidget(widget->ui->glOutbound);

  layout->addWidget(widget);
}

void VHttpProxy::optionSaveDlg(QDialog* dialog)
{
  VHttpProxyWidget* widget = dialog->findChild<VHttpProxyWidget*>("httpProcyWidget");
  LOG_ASSERT(widget != NULL);

  tcpEnabled = widget->findChild<QCheckBox*>("chkTcpEnabled")->checkState() == Qt::Checked;
  sslEnabled = widget->findChild<QCheckBox*>("chkSslEnabled")->checkState() == Qt::Checked;

  outPolicy.optionSaveDlg((QDialog*)widget->ui->tabExternal);
  tcpServer.optionSaveDlg((QDialog*)widget->ui->tabTcpServer);
  sslServer.optionSaveDlg((QDialog*)widget->ui->tabSslServer);
  inboundDataChange.optionSaveDlg((QDialog*)widget->ui->tabInbound);
  outboundDataChange.optionSaveDlg((QDialog*)widget->ui->tabOutbound);
}
#endif // QT_GUI_LIB
