#include "App.hpp"

App::App() {
}

App::~App() {
}

void App::run() {
    while (m_window->isOpen()) {
        m_window->pollEvents();
    }
}
