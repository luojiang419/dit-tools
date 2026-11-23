#pragma once

#include <memory>

class AppContext;
class QQmlApplicationEngine;

class AppBootstrap {
public:
    AppBootstrap();
    ~AppBootstrap();

    bool run();
    bool activateMainWindow();

private:
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};
