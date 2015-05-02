TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += c++11

SOURCES += main.cpp

QMAKE_CXXFLAGS += -Wextra
contains(QMAKE_HOST.arch, armv6l){
    linux-g++{
        QT -= core gui
        QMAKE_CXXFLAGS += -std=c++11
        LIBS += -lboost_thread -lboost_system -lboost_coroutine -lboost_context -lpthread -lboost_regex
    }
}
contains(QMAKE_HOST.arch, x86_64){
    linux{
        QMAKE_CXXFLAGS += -Wextra -Wall -DBOOST_USE_VALGRIND -isystem /home/chenfengyuan/.local_boost_1_57/include/
        INCLUDEPATH += /home/chenfengyuan/.local_boost_1_57/include/
        LIBS += -L/home/chenfengyuan/.local_boost_1_57/lib -lpthread -lboost_thread -lboost_system -lboost_coroutine -lboost_context -lboost_regex
    }
    darwin{
        QMAKE_MAC_SDK=macosx10.9
        QMAKE_CXXFLAGS += -Wno-c++14-extensions
        INCLUDEPATH += "/usr/local/Cellar/boost/1.56.0/include/"
        LIBS += -L/usr/local/Cellar/boost/1.56.0/lib -lboost_thread-mt -lboost_system-mt -lboost_coroutine-mt -lboost_context-mt -lboost_regex-mt
    }
}
