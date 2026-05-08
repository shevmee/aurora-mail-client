#include "MailSessionSignals.hpp"

#include <QMetaType>

Q_DECLARE_METATYPE(ImapSessionLinkState)

namespace
{
struct RegisterMailSessionMetaTypes
{
    RegisterMailSessionMetaTypes()
    {
        qRegisterMetaType<ImapSessionLinkState>();
    }
} const registerMailSessionMetaTypes;
}  // namespace
