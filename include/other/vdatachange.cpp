#include "vdatachange.h"

// ----------------------------------------------------------------------------
// VDataChangeItem
// ----------------------------------------------------------------------------
VDataChangeItem::VDataChangeItem()
{
  enabled    = true;
  pattern    = "";
  syntax     = QRegExp::FixedString;
  cs         = Qt::CaseSensitive;
  minimal    = false;
  replace    = true;
  replaceStr = "";
}

bool VDataChangeItem::prepare(VError& error)
{
  // gilgil temp 2014.03.15 order ???
  rx.setPattern(pattern);
  rx.setCaseSensitivity(cs);
  rx.setMinimal(minimal);
  rx.setPatternSyntax(syntax);

  if (!rx.isValid())
  {
    SET_ERROR(VError, qformat("rx is not valid(%s)", qPrintable(pattern)), VERR_UNKNOWN);
    return false;
  }

  return true;
}

bool VDataChangeItem::change(QByteArray& ba, bool* found)
{
  QString text = QString::fromLatin1(ba);
  bool changed = false;
  int offset = 0;

  while (true)
  {
    int index = rx.indexIn(text, offset);
    if (index == -1) break;

    offset       = index;
    QString from = rx.cap(0);

    if (!replace)
    {
      if (found != NULL) *found = true;
      if (log)
      {
        LOG_INFO("found   \"%s\"", qPrintable(from));
      }
      offset++;
      continue;
    }
    // LOG_DEBUG("index=%d", index); // gilgil temp 2014.03.15

    if (from == replaceStr) continue;
    text.replace(index, from.length(), replaceStr);
    changed = true;
    if (log)
    {
      LOG_INFO("changed \"%s\" > \"%s\"", qPrintable(from), qPrintable(replaceStr));
    }
  }
  if (changed)
  {
    ba = text.toLatin1();
  }
  return changed;
}

void VDataChangeItem::load(VXml xml)
{
  enabled = xml.getBool("enabled", enabled);
  log     = xml.getBool("log", log);
  pattern = xml.getStr("pattern", pattern);
  syntax  = (QRegExp::PatternSyntax)xml.getInt("syntax", (int)syntax);
  cs      = (Qt::CaseSensitivity)xml.getInt("cs", (int)cs);
  minimal = xml.getBool("minimal", minimal);
  replace = xml.getBool("replace", replace);
  replaceStr = qPrintable(xml.getStr("replaceStr", QString(replaceStr)));
}

void VDataChangeItem::save(VXml xml)
{
  xml.setBool("enabled", enabled);
  xml.setBool("log", log);
  xml.setStr("pattern", pattern);
  xml.setInt("syntax", (int)syntax);
  xml.setInt("cs", (int)cs);
  xml.setBool("minimal", minimal);
  xml.setBool("replace", replace);
  xml.setStr("replaceStr", QString(replaceStr));
}

// ----------------------------------------------------------------------------
// VDataChange
// ----------------------------------------------------------------------------
VDataChange::VDataChange()
{
}

VDataChange::~VDataChange()
{
}

bool VDataChange::prepare(VError& error)
{
  for (int i = 0; i < count(); i++)
  {
    VDataChangeItem& item = (VDataChangeItem&)at(i);
    if (!item.prepare(error)) return false;
  }
  return true;
}

bool VDataChange::change(QByteArray& ba, bool* found)
{
  bool _changed = false;
  for (int i = 0; i < count(); i++)
  {
    VDataChangeItem& item = (VDataChangeItem&)at(i);
    if (!item.enabled) continue;
    if (item.change(ba, found))
    {
      _changed = true;
    }
  }
  return _changed;
}

void VDataChange::load(VXml xml)
{
  clear();
  {
    xml_foreach (childXml, xml.childs())
    {
      VDataChangeItem item;
      item.load(childXml);
      push_back(item);
    }
  }
}

void VDataChange::save(VXml xml)
{
  xml.clearChild();
  for (VDataChange::iterator it = begin(); it != end(); it++)
  {
    VDataChangeItem& item = *it;
    VXml childXml = xml.addChild("item");
    item.save(childXml);
  }
}

#ifdef QT_GUI_LIB
#include "vdatachangewidget.h"
#include "ui_vdatachangewidget.h"
void VDataChange::optionAddWidget(QLayout* layout)
{
  VDataChangeWidget* widget = new VDataChangeWidget(layout->parentWidget());
  widget->setObjectName("dataChangeWidget");
  *(widget->ui->treeWidget) << *this;
  layout->addWidget(widget);
}

void VDataChange::optionSaveDlg(QDialog* dialog)
{
  VDataChangeWidget* widget = dialog->findChild<VDataChangeWidget*>("dataChangeWidget");
  LOG_ASSERT(widget != NULL);
  *this << *(widget->ui->treeWidget);
}

#endif // QT_GUI_LIB
