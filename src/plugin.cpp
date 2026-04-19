#include <QQmlExtensionPlugin>
#include <QQmlEngine>
#include <array>
#ifdef BUILD_MPV
#include "MpvBackend.hpp"
#endif
#ifdef BUILD_SCENE
#include "SceneBackend.hpp"
#endif
#include "MouseGrabber.hpp"
#include "TTYSwitchMonitor.hpp"
#include "PluginInfo.hpp"
#include "FileHelper.hpp"
#include "GamemodeMonitor.hpp"

constexpr std::array<uint, 2> WPVer { 1, 2 };

class Port : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char* uri) override {
        if (strcmp(uri, "com.github.catsout.wallpaperEngineKde") != 0) return;
        qmlRegisterType<wekde::PluginInfo>(uri, WPVer[0], WPVer[1], "PluginInfo");
        qmlRegisterType<wekde::MouseGrabber>(uri, WPVer[0], WPVer[1], "MouseGrabber");
#ifdef BUILD_SCENE
        qmlRegisterType<scenebackend::SceneObject>(uri, WPVer[0], WPVer[1], "SceneViewer");
#endif
        std::setlocale(LC_NUMERIC, "C");
#ifdef BUILD_MPV
        qmlRegisterType<mpv::MpvObject>(uri, WPVer[0], WPVer[1], "Mpv");
#endif
        qmlRegisterType<wekde::TTYSwitchMonitor>(uri, WPVer[0], WPVer[1], "TTYSwitchMonitor");
        qmlRegisterType<wekde::FileHelper>(uri, WPVer[0], WPVer[1], "FileHelper");
        qmlRegisterType<wekde::GamemodeMonitor>(uri, WPVer[0], WPVer[1], "GamemodeMonitor");
    }
};

#include "plugin.moc"
