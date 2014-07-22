#include "shoppingpage.h"
#include "ui_shoppingpage.h"

#include "addresstablemodel.h"
#include "optionsmodel.h"
#include "bitcoingui.h"
#include "editaddressdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QScrollArea>
#include <QtWebKit/QWebView>
#include <QMenu>
#include <QUrl>
#include <QDesktopServices>
#ifdef USE_QRCODE
#include "qrcodedialog.h"
#endif

ShoppingPage::ShoppingPage(Mode mode, Tabs tab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ShoppingPage),
    model(0),
    optionsModel(0),
    mode(mode),
    tab(tab)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->newAddressButton->setIcon(QIcon());
    ui->copyToClipboard->setIcon(QIcon());
#endif



    switch(mode)
    {
    case ForSending:
        connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
        ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->tableView->setFocus();
        break;
    case ForEditing:
        break;
    }
    switch(tab)
    {
    case SendingTab:
        break;
    case ReceivingTab:
        break;
    }

    // Context menu actions
    QAction *copyLabelAction = new QAction(tr("Copy &Label"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(editAction);

    // Connect signals for context menu actions
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(onCopyLabelAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

}

ShoppingPage::~ShoppingPage()
{
    delete ui;
}

void ShoppingPage::setModel(AddressTableModel *model)
{
    this->model = model;
    if(!model)
        return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    switch(tab)
    {
    case ReceivingTab:
        // Receive filter
        proxyModel->setFilterRole(AddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(AddressTableModel::Receive);
        break;
    case SendingTab:
        // Send filter
        proxyModel->setFilterRole(AddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(AddressTableModel::Send);
        break;
    }
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
    ui->tableView->horizontalHeader()->resizeSection(
            AddressTableModel::Address, 320);
    ui->tableView->horizontalHeader()->setResizeMode(
            AddressTableModel::Label, QHeaderView::Stretch);

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    // Select row for newly created address
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(selectNewAddress(QModelIndex,int,int)));

    selectionChanged();
}

void ShoppingPage::setOptionsModel(OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
}


void ShoppingPage::onCopyLabelAction()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Label);
}


void ShoppingPage::onEditAction()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditAddressDialog dlg(
            tab == SendingTab ?
            EditAddressDialog::EditSendingAddress :
            EditAddressDialog::EditReceivingAddress);
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}


void ShoppingPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        switch(tab)
        {
        case SendingTab:
            // In sending tab, allow deletion of selection
            deleteAction->setEnabled(true);
            break;
        case ReceivingTab:
            // Deleting receiving addresses, however, is not allowed
            deleteAction->setEnabled(false);
            break;
        }
    }

}

void ShoppingPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;
    // When this is a tab/widget and not a model dialog, ignore "done"
    if(mode == ForEditing)
        return;

    // Figure out which address was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    foreach (QModelIndex index, indexes)
    {
        QVariant address = table->model()->data(index);
        returnValue = address.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no address entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void ShoppingPage::exportClicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Address Book Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Label", AddressTableModel::Label, Qt::EditRole);
    writer.addColumn("Address", AddressTableModel::Address, Qt::EditRole);

    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



//{
 //   QString link="http://www.ecasino.io/";
//    QDesktopServices::openUrl(QUrl(link));
//}


void ShoppingPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void ShoppingPage::selectNewAddress(const QModelIndex &parent, int begin, int end)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, AddressTableModel::Address, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newAddressToSelect))
    {
        // Select row of newly created address, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newAddressToSelect.clear();
    }
}


//Exchange Links
void ShoppingPage::on_pushButton_36_clicked()
{
    QString link="https://www.cryptsy.com/markets/view/134";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_39_clicked()
{
    QString link="https://www.cryptsy.com/markets/view/108";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_37_clicked()
{
    QString link="https://bter.com/trade/net_cny";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_38_clicked()
{
    QString link="https://bter.com/trade/net_ltc";
    QDesktopServices::openUrl(QUrl(link));
}


//Shopping Links
void ShoppingPage::on_pushButton_40_clicked()
{
    QString link="http://alpenpowerpro.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_43_clicked()
{
    QString link="http://btcpipeshop.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_46_clicked()
{
    QString link="http://bitezze.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_41_clicked()
{
    QString link="http://www.cryptodirect.cf/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_44_clicked()
{
    QString link="http://www.grkreationsdirect.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_47_clicked()
{
    QString link="http://www.thestakingmachine.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_42_clicked()
{
    QString link="http://www.mintagemastermind.com/index.html";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_45_clicked()
{
    QString link="http://www.picca.co.uk/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_48_clicked()
{
    QString link="http://www.retrotowers.co.uk/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_79_clicked()
{
    QString link="http://scryptstore.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_78_clicked()
{
    QString link="http://zetasteam.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_49_clicked()
{
    QString link="http://thestakingmachine.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_81_clicked()
{
    QString link="http://tuffwraps.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_80_clicked()
{
    QString link="http://waterido.com/";
    QDesktopServices::openUrl(QUrl(link));
}

void ShoppingPage::on_pushButton_82_clicked()
{
    QString link="http://wrol.info/survival-gear-bitcoin/";
    QDesktopServices::openUrl(QUrl(link));
}
