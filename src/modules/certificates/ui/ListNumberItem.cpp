#include "modules/certificates/ui/ListNumberItem.h"

#include "modules/certificates/domain/Certificate.h"

ListNumberItem::ListNumberItem(const QString& listNumber)
    : QTableWidgetItem(listNumber)
{
}

bool ListNumberItem::operator<(const QTableWidgetItem& other) const
{
    // The ordering rule itself lives in the domain, where it is testable
    // without a table on screen.
    return CertificateListNumber::lessThan(text(), other.text());
}
