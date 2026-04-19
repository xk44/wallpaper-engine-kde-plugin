#include "PluginInfo.hpp"
#include <iostream>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QUrl>

#ifdef BUILD_SCENE
#include "SceneBackend.hpp"
#endif

using namespace wekde;

PluginInfo::PluginInfo(QObject* parent): QObject(parent) {}

PluginInfo::~PluginInfo() {}

QUrl PluginInfo::cache_path() const {
#ifdef BUILD_SCENE
    return QUrl::fromLocalFile(
        QString::fromStdString(scenebackend::SceneObject::GetDefaultCachePath()));
#else
    return QUrl::fromLocalFile(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
#endif
}
