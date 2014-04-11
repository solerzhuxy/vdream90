#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <VHttpProxy>

// ----------------------------------------------------------------------------
// MsgEvent
// ----------------------------------------------------------------------------
class MsgEvent : public QEvent
{
public:
  QString    msg;
  Qt::HANDLE threadId;
public:
  MsgEvent(QString msg, Qt::HANDLE threadId) : QEvent(None)
  {
    this->msg      = msg;
    this->threadId = threadId;
  }
};

// ----------------------------------------------------------------------------
// Widget
// ----------------------------------------------------------------------------
namespace Ui {
  class Widget;
}

class Widget : public QWidget, public VXmlable
{
  Q_OBJECT

public:
  explicit Widget(QWidget *parent = 0);
  ~Widget();

public:
  void initializeControl();
  void finalizeControl();
  void loadControl();
  void saveControl();
  void setControl(VState state = VState::None);

public:
  bool       showMsg;
  VHttpProxy proxy;

protected:
  virtual bool event(QEvent* event);
  virtual void showEvent(QShowEvent* showEvent);
  void         showMessage(MsgEvent* event);

public slots:
  void httpRequestHeader (VHttpRequest*  request,  VHttpProxyConnection* connection);
  void httpResponseHeader(VHttpResponse* response, VHttpProxyConnection* connection);
  void httpRequestBody   (QByteArray*    body,     VHttpProxyConnection* connection);
  void httpResponseBody  (QByteArray*    body,     VHttpProxyConnection* connection);

public:
  virtual void load(VXml xml);
  virtual void save(VXml xml);

private slots:
  void on_pbOpen_clicked();

  void on_pbClose_clicked();

  void on_pbClear_clicked();

  void on_pbOption_clicked();
  void on_chkShowMsg_clicked();

private:
  Ui::Widget *ui;
};

#endif // WIDGET_H
