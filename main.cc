//***************************************************************************
// File main.cc
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat
//***************************************************************************

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "def.h"
#include "clusterchat.hpp"
#include "thread.hpp"
#include "ini.h"

//***************************************************************************
// globals
//***************************************************************************

CondVar mainCond;
Mutex mainMutex;
int shouldExit = no;
char* lastSection= 0;
ServerConfig* lastConfig= 0;

int doHelp();
void doCopyright();
int doCommandline(int argc, char* argv[], std::list<ServerConfig*>* configs);
int parseServer(char* string, ServerConfig* cfg);
int iniHandler(void* user, const char* section, const char* name, const char* value);

int Globals::cfgVerbose= 0;
int Globals::cfgDebug= 0;

//***************************************************************************
// signal processing
//***************************************************************************

static void* sig_thread(void *arg)
{
   int sig;
   sigset_t *set = (sigset_t*)arg;

   for (;;) 
   {
      if (!sigwait(set, &sig))
      {
         printf("[SIG Handler] Got signal: %s\n", strsignal(sig));

         if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
         {
            printf("[SIG Handler] Exiting ...\n");
            shouldExit= yes;
            mainMutex.lock();
            mainCond.broadcast();
            mainMutex.unlock();
            break;
         }
      }
   }

   return 0;
}

//***************************************************************************
// main
//***************************************************************************

int main(int argc, char* argv[])
{
   int res= success;
   std::list<ServerConfig*> configs;

   if (argc == 1 || (argv[1] && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "-?") || !strcmp(argv[1], "--help"))))
      return doHelp();

   doCopyright();
   
   if (doCommandline(argc, argv, &configs) != success)
      return fail;

   printf("Main: Using servers:\n");

   for (std::list<ServerConfig*>::iterator it= configs.begin(); it != configs.end(); ++it)
      printf("Main:  - %s@%s:%d\n", (*it)->title.c_str(), (*it)->host.c_str(), (*it)->port);

   printf("Main: Starting (debug: %d, verbose: %d).\n", Globals::cfgDebug, Globals::cfgVerbose);

   mainMutex.lock();

   sigset_t set;
   pthread_t handleThread;
   ClusterChat clusterChat;

   // Block SIGQUIT and SIGUSR1; other threads created by main()
   // will inherit a copy of the signal mask.

   sigemptyset(&set);
   sigaddset(&set, SIGQUIT);
   sigaddset(&set, SIGTERM);
   sigaddset(&set, SIGINT);

   pthread_sigmask(SIG_BLOCK, &set, NULL);
   pthread_create(&handleThread, NULL, &sig_thread, (void *) &set);

   res= clusterChat.init(&configs);

   if (res)
   {
      fprintf(stderr, "ClusterChat: Error: Failed to start worker threads (%d), exiting ...\n", res);
      clusterChat.shutdown();
      return -1;
   }

   while (!shouldExit)
      mainCond.timedWait(mainMutex, 3);

   printf("Main: Exiting.\n");

   pthread_join(handleThread, 0);
   clusterChat.shutdown();

   return 0;
}

//***************************************************************************
// main
//***************************************************************************

int doHelp()
{
   doCopyright();

   printf("\nUsage:\n");
   printf("   $ arkclusterchat [OPTIONS]\n");
   printf("Options:\n");
   printf("      -s [SERVER]    Add servers in the format: [TITLE]:[RCONPASSWORD]@[HOST]:[RCONPORT].\n");
   printf("                     For example '-s TheIsland:greatPassw0rd@myhost:32330'.\n");
   printf("                     Option can be repeated to set multiple servers. Title will be printed in\n");
   printf("                     game chat as well as application log. Make sure to use your server RCON port.'\n\n");
   printf("      -c [FILE]      Path to ini configuration file with server descriptions (as alternative to -s option).\n\n");
   printf("      --verbose      Print all messages sent/received.\n");
   printf("      --debug        Send chat messages ONLY to the server they have been received on.\n\n");
   printf("The configuration file should have the following contents PER SERVER:\n\n");
   printf(" [(TITLE)]\n");
   printf(" host = (HOSTNAME)\n");
   printf(" port = (RCONPORT)\n");
   printf(" password = (RCONPASSWORD)\n\n");
   printf(" Example:\n\n");
   printf(" [TheIsland]\n");
   printf(" host = 123.123.123.123\n");
   printf(" port = 32330\n");
   printf(" password = greatPassw0rd\n\n");
   printf(" [ScorchedEarth]\n");
   printf(" host = some.greathost.com\n");
   printf(" port = 32331\n");
   printf(" password = \"greatPassw0rdT00\"\n\n");
   printf("Notes:\n");
   printf(" - Server titles must not contain spaces or special characters.\n");
   printf(" - Server titles in the configuration file must be UNIQUE.\n");

   return 0;
}

//***************************************************************************
// main
//***************************************************************************

void doCopyright()
{
   printf("ClusterChat - Ark cross server chat application\n");
   printf("Copyright (c) 2017 by s710\n");
   printf("GitHub: https://github.com/patrickjane/arkclusterchat\n");
}

//***************************************************************************
// doCommandline
//***************************************************************************

int doCommandline(int argc, char* argv[], std::list<ServerConfig*>* configs)
{
   int i= 0;
   const char* configFile= 0;

   while (++i < argc && argv[i])
   {
      if (!strcmp(argv[i], "-c") && argv[i+1])
      {
         configFile= argv[i+1];
         i++;
      }

      if (!strcmp(argv[i], "-s") && argv[i+1])
      {
         ServerConfig* cfg= new ServerConfig();

         if (parseServer(argv[i+1], cfg) != success)
         {
            fprintf(stderr, "Error: Failed to parse malformed server '%s'\n", argv[i+1]);
            delete cfg;
            return fail;
         }

         configs->push_back(cfg);
         i++;
      }

      if (!strcmp(argv[i], "--verbose"))
      {
         Globals::cfgVerbose= 1;
         continue;
      }
 
      if (!strcmp(argv[i], "--debug"))
      {
         Globals::cfgDebug= 1;
         continue;
      }
   }

   if (configFile)
   {
      for (std::list<ServerConfig*>::iterator it= configs->begin(); it != configs->end(); ++it)
         delete *it;

      configs->clear();
         
      printf("Main: Using configuration file '%s'\n", configFile);

      if (ini_parse(configFile, iniHandler, configs) < 0)
      {
         fprintf(stderr, "Error: Failed to parse file '%s'\n", configFile);
         return fail;
      }
   }

   return done;
}

//***************************************************************************
// parseServer
//***************************************************************************

int parseServer(char* string, ServerConfig* cfg)
{
   // expect: title:password@host:port
   // parse from both sides, to allow passwords with special characters.
   // title is expected to have NO spaces and NO special characters.

   if (!string || !strlen(string) || !cfg)
      return fail;


   char* p= 0;
   char* passwordStart= 0;
   char* end= 0;

   p= passwordStart= strchr(string, ':');
   passwordStart++;

   if (!p)
      return fail;

   cfg->title.assign(string, p-string);

   p= string+strlen(string)-1;

   while (*p && *p != ':' && p > string)
      p--;

   if (*p != ':')
      return fail;

   cfg->port= atoi(p+1);

   end= p-1;

   while (*p && *p != '@' && p > string)
      p--;

   if (*p != '@')
      return fail;

   cfg->host.assign(p+1, end-p);
   cfg->password.assign(passwordStart, p-passwordStart);

   return success;
}


//***************************************************************************
// iniHandler
//***************************************************************************

int iniHandler(void* user, const char* section, const char* name, const char* value)
{
   std::list<ServerConfig*>* configs= (std::list<ServerConfig*>*)user;

   if (!lastSection)
   {
      // first section

      lastSection= strdup(section);
      lastConfig= new ServerConfig();
      lastConfig->title.assign(section);
      configs->push_back(lastConfig);
   }
   else if (strcmp(lastSection, section))
   {
      // next section, append last section/server

      ::free((void*)lastSection);
      lastSection= strdup(section);
      lastConfig= new ServerConfig();
      lastConfig->title.assign(section);
      configs->push_back(lastConfig);
   }

   if (!strcmp(name, "host")) lastConfig->host.assign(value);
   else if (!strcmp(name, "password")) lastConfig->password.assign(value);
   else if (!strcmp(name, "port")) lastConfig->port= atoi(value);

   return 1;
}

