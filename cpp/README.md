# port forward
simple tcp forwarder using Boost ASIO
## Build:
1. edit `port_forward.pro` (change include/lib paths, and you may need to remove `-DBOOST_USE_VALGRIND`)
1. `qmake && make`
## Usage:

`./port_forward listen_host listen_port destination_host destination_port`

