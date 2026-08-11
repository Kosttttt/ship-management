#include "core/InstallationContext.h"

InstallationContext::InstallationContext(const InstallationRecord& record)
    : m_record(record)
    , m_configured(true)
{
}

bool InstallationContext::isConfigured() const
{
    return m_configured;
}

InstallationMode InstallationContext::mode() const
{
    return m_record.mode;
}

QString InstallationContext::nodeId() const
{
    return m_record.nodeId;
}

QString InstallationContext::vesselId() const
{
    return m_record.vesselId;
}

QString InstallationContext::vesselName() const
{
    return m_record.vesselName;
}

QString InstallationContext::vesselImoNumber() const
{
    return m_record.vesselImoNumber;
}

QString InstallationContext::vesselScope() const
{
    return (m_record.mode == InstallationMode::Vessel) ? m_record.vesselId : QString();
}
