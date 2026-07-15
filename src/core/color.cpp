#include "color.h"
#include <lcms2.h>
#include <QFile>

static cmsHPROFILE s_monitorProfile = nullptr;
static bool s_initialized = false;

bool ColorManager::init(bool detectMonitor)
{
    s_initialized = false;
    s_monitorProfile = nullptr;

    if (!detectMonitor) return true;

    // Windows: 从 GetICMProfile 等 API 读取
    // Linux: 从 colord D-Bus 获取
    // macOS: 从 NSScreen 获取
    // 此处为跨平台占位，平台实现在 platform/ 目录
    s_initialized = true;
    return true;
}

bool ColorManager::loadProfile(const QString &iccPath)
{
    QFile f(iccPath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QByteArray data = f.readAll();
    cmsHPROFILE prof = cmsOpenProfileFromMem(data.constData(), (cmsUInt32Number)data.size());
    if (!prof) return false;

    if (s_monitorProfile) cmsCloseProfile(s_monitorProfile);
    s_monitorProfile = prof;
    s_initialized = true;
    return true;
}

bool ColorManager::hasProfile()
{
    return s_monitorProfile != nullptr;
}
