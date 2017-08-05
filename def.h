//***************************************************************************
// File def.h
// Date 24.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / globals
//***************************************************************************

#ifndef __DEF_HPP__
#define __DEF_HPP__

//***************************************************************************
// global defines
//***************************************************************************

enum ReturnValues
{
   success= 0,
   fail= -1,
   na= fail,
   yes= 1,
   no= !yes,
   done= success
};

enum State
{
   isUnknown= na,

   isRunning,
   isExit,
   isStopped
};

enum Errors
{
   errFirst= -99,

   errWrongSequence,
   wrnNoResponse
};

// global flags

class Globals
{
   public:

      static int cfgVerbose;
      static int cfgDebug;
};


//***************************************************************************
#endif // __DEF_HPP__
