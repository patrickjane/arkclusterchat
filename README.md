# arkclusterchat
Cluster chat application for Ark Survival Evolved

`arkclusterchat` is a small program which connects to multiple gameservers of Ark Survival Evolved using the RCON protocol. It reads the chat messages on each server, and forwards this message to the other servers. This way, "cluter chat" support is achieved.
The messages send by `arkclusterchat` will be shown as server messages (yellow), and have a prefix with the server title for each message.
## History
**Version 1.0** - released august 6th 2017.    

- Initial release    

**Version 1.1** - released january 27th 2018.    

- Added option to show/suppress admin commands (`--show-admin-cmd`)


## Compilation
Linux/MacOS is supported, Windows is **not** supported. Simply clone this repository and run `make -s` to compile the program.

    steam@blashyrkh:~/clusterchat$ git clone https://github.com/patrickjane/arkclusterchat.git .
    Cloning into '.'...
    remote: Counting objects: 30, done.
    remote: Compressing objects: 100% (23/23), done.
    remote: Total 30 (delta 6), reused 23 (delta 6), pack-reused 0
    Unpacking objects: 100% (30/30), done.
    Checking connectivity... done.
    steam@blashyrkh:~/clusterchat$ make -s
    -
    Making arkclusterchat ...
    Compile main ...
    Compile channel ...
    Compile thread ...
    Compile rconthread ...
    Compile clusterchat ...
    Compile ini ...
    Linking  ...

You will find a binary named `arkclusterchat` in the same folder.

## Binary downloads
I have never created binary releases, but it ***should*** work, in case you don't want to download the source and compile the tool:

- **Version 1.1** - http://filehorst.de/d/cAayrquG (Linux 64bit binary, libstdc++.6)
- **Version 1.1** - http://filehorst.de/d/ccyvFxwG (MacOS)

## Usage
`arkclusterchat` needs RCON support on your server, but it does not need to run on the server itself. Actually, it can run just anywhere as long as you have network access to your server's RCON port.
The program needs the connection parameters for each host you want to connect to, which can be either given via the `-s` command line options, or stored in a separate configuration file, which is used using the `-c` command line option.

    Wildfire:clusterchat s710$ ./arkclusterchat -h
    ClusterChat - Ark cross server chat application version 1.1
    Copyright (c) 2017 by s710
    GitHub: https://github.com/patrickjane/arkclusterchat

    Usage:
       $ arkclusterchat [OPTIONS]
    Options:
          -s [SERVER]       Add servers in the format: [TITLE]:[RCONPASSWORD]@[HOST]:[RCONPORT].
                            For example '-s TheIsland:greatPassw0rd@myhost:32330'.
                            Option can be repeated to set multiple servers. Title will be printed in
                            game chat as well as application log. Make sure to use your server RCON port.'

          -c [FILE]         Path to ini configuration file with server descriptions (as alternative to -s option).

          --verbose         Print all messages sent/received (default: disabled).
          --debug           Send chat messages ONLY to the server they have been received on (default: disabled).
          --show-admin-cmd  Also show any admin commands sent through console (default: disabled).

### Config file
The configuration file should have the following contents PER SERVER:

     [(TITLE)]    
     host = (HOSTNAME)    
     port = (RCONPORT)    
     password = (RCONPASSWORD)    

 Example:

     [TheIsland]    
     host = 123.123.123.123    
     port = 32330    
     password = greatPassw0rd    

     [ScorchedEarth]    
     host = some.greathost.com    
     port = 32331    
     password = "greatPassw0rdT00"    

Notes:
 - Server titles must not contain spaces or special characters.
 - Server titles in the configuration file must be UNIQUE.
 
 ## Log output
 Normal log output of the program should look smiliar to this:
 
    Wildfire:clusterchat s710$ ./arkclusterchat -c test.ini --verbose
    ClusterChat - Ark cross server chat application version 1.1
    Copyright (c) 2017 by s710
    GitHub: https://github.com/patrickjane/arkclusterchat
    Main: Using configuration file 'test.ini'
    Main: Using servers:
    Main:  - ScorchedEarth@xxx.xxx.xxx.xxx:32330
    Main:  - TheCenter@xxx.xxx.xxx.xxx:32331
    Main:  - TheIsland@xxx.xxx.xxx.xxx:32332
    Main:  - Ragnarok@xxx.xxx.xxx.xxx:32333
    Main: Starting (show admin cmd: 0, debug: 0, verbose: 1).
    [ScorchedEarth] Connected to host xxx.xxx.xxx.xxx:32330
    [TheCenter] Connected to host xxx.xxx.xxx.xxx:32331
    [TheIsland] Connected to host xxx.xxx.xxx.xxx:32332
    [Ragnarok] Connected to host xxx.xxx.xxx.xxx:32333

    ^[^C[SIG Handler] Got signal: Interrupt: 2
    [SIG Handler] Exiting ...
    Main: Exiting.
    ClusterChat: Stopping worker threads ...
    [ScorchedEarth] Shutting down
    [TheCenter] Shutting down
    [TheIsland] Shutting down
    [Ragnarok] Shutting down
