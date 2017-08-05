//***************************************************************************
// File clusterchat.hpp
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / ClusterChat class
//***************************************************************************

#ifndef __CLUSTERCHAT_HPP__
#define __CLUSTERCHAT_HPP__

#include <list>                   // std::list
#include <string>                 // std::string
#include "rconthread.hpp"

//***************************************************************************
// struct ServerConfig
//***************************************************************************

struct ServerConfig
{
   std::string host;
   std::string password;
   std::string title;
   int port;
};

//***************************************************************************
// class ClusterChat
//***************************************************************************

class ClusterChat
{
   public:

      ClusterChat();
      ~ClusterChat();

      int init(std::list<ServerConfig*>* configs);
      int shutdown();

   protected:

      std::list<RConThread*> threads;
};

//***************************************************************************
#endif //__CLUSTERCHAT_HPP__
