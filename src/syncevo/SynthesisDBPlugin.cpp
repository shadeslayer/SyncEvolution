/*
 * This file implements the Synthesis DB API and maps it to
 * EvolutionSyncSources. It was derived from the Synthesis
 * sync_dbapi_demo.c file.
 *
 * The external API of this file are the globally visible
 * C functions defined by sync_dbapidef.h.
 * 
 * Copyright (c) 2004-2008 by Synthesis AG
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include <synthesis/sync_include.h>   /* include general SDK definitions */
#include <synthesis/sync_dbapidef.h>  /* include the interface file and utilities */
#include <synthesis/SDK_util.h>       /* include SDK utilities */

using namespace sysync;

#include <syncevo/SyncContext.h>
#include <syncevo/SyncSource.h>

#include <sstream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

#define BuildNumber  0  /* User defined build number, can be 0..255 */
#define MyDB   "SyncEvolution" /* example debug name */
#define MY_ID       42  /* example datastore context */

#define STRLEN      80  /* Max length of local string copies */

/* -- MODULE -------------------------------------------------------------------- */
/* <mContext> will be casted to the SyncSource * structure */
static SyncSource *MoC(CContext mContext) { return (SyncSource *)mContext; }

/**
 * looks up datasource and uses pointer to it as context
 *
 * @param mContextName   name of previously instantiated SyncSource
 * @retval mContext      the corresponding SyncSource
 */
extern "C"
TSyError SyncEvolution_Module_CreateContext( CContext *mContext, cAppCharP   moduleName,
                                             cAppCharP      subName,
                                             cAppCharP mContextName,
                                             DB_Callback mCB )
{
    TSyError err = LOCERR_WRONGUSAGE;
    SyncSource *source = SyncContext::findSource(mContextName);
    if (source) {
        source->pushSynthesisAPI(mCB);
        *mContext = (CContext)source;
        err = LOCERR_OK;
    }

    DEBUG_DB(mCB, MyDB,  Mo_CC, "'%s%s%s' (%s) => %d",
             moduleName, subName[0] ? "/" : "", subName, 
             mContextName,
             err);
    return err;
}



/**
 * @TODO: introduce and return some kind of SyncEvolution build number
 */
extern "C"
CVersion SyncEvolution_Module_Version(CContext mContext) 
{
    CVersion v = Plugin_Version(BuildNumber);
  
    if (mContext) {
        DEBUG_DB(MoC(mContext)->getSynthesisAPI(), MyDB,Mo_Ve, "%08X", v);
    }
    return v;
}



/* Get the plug-in's capabilities */
extern "C"
TSyError SyncEvolution_Module_Capabilities( CContext mContext, appCharP *mCapabilities )
{
    std::stringstream s;
    s << MyPlatform() << "\n"
      << DLL_Info << "\n"
      << CA_MinVersion << ":V1.0.6.0\n" /* must not be changed */
      << CA_Manufacturer << ":SyncEvolution\n"
      << CA_Description << ":SyncEvolution Synthesis DB Plugin\n"
      << Plugin_DS_Data_Str << ":no\n"
      << Plugin_DS_Data_Key << ":yes\n"
      << CA_ItemAsKey << ":yes\n"
      << Plugin_DS_Blob << ":no\n";
    *mCapabilities= StrAlloc(s.str().c_str());
    DEBUG_DB(MoC(mContext)->getSynthesisAPI(), MyDB, Mo_Ca, "'%s'", *mCapabilities);
    return LOCERR_OK;
} /* Module_Capabilities */



extern "C"
TSyError SyncEvolution_Module_PluginParams( CContext mContext,
                                            cAppCharP mConfigParams, CVersion engineVersion )
{
    SyncSource *source  = MoC(mContext);
    DEBUG_DB(source->getSynthesisAPI(), MyDB, Mo_PP, " Engine=%08X", engineVersion);
    DEBUG_DB(source->getSynthesisAPI(), MyDB, Mo_PP, "'%s'",         mConfigParams );
  
    /*return LOCERR_CFGPARSE;*/ /* if there are unsupported params */
    return LOCERR_OK;
} /* Module_PluginParams */



/* Dispose the memory of the module context */
extern "C"
void SyncEvolution_Module_DisposeObj( CContext mContext, void* memory )
{
    DEBUG_Exotic_DB(MoC(mContext)->getSynthesisAPI(), MyDB, Mo_DO,
                    "free at %08X '%s'", memory, memory);
    StrDispose(memory);
}

extern "C"
TSyError SyncEvolution_Module_DeleteContext( CContext mContext )
{
    SyncSource  *source = MoC(mContext);
    DEBUG_DB(source->getSynthesisAPI(), MyDB, Mo_DC, "'%s'", source->getName());
    source->popSynthesisAPI();
    return LOCERR_OK;
}




/* ---------------------- session handling --------------------- */
/* this is an example, how a context could be structured */
class SessionContext {
public:
    SessionContext() { fCB= NULL; }
    
    int         fID; /* a reference number. */
    DB_Callback fCB; /* callback structure  */
    int      fPMode; /* The login password mode */
};

/* <sContext> will be casted to the SessionContext* structure */
static SessionContext* SeC( CContext sContext ) { return (SessionContext*)sContext; }



/* Create a context for a new session */ 
extern "C"
TSyError SyncEvolution_Session_CreateContext( CContext *sContext, cAppCharP sessionName, DB_Callback sCB )
{ 
  SessionContext* sc;
  
/*return DB_Error;*/ /* added for test */
  
  sc= new SessionContext;

  if (sc==NULL) return DB_Full;

  /**** CAN BE ADAPTED BY USER ****/
            sc->fID= 333; /* as an example */
            sc->fCB= sCB;
            sc->fPMode= Password_ClrText_IN;        /* take this mode ... */
         /* sc->fPMode= Password_ClrText_OUT; */    /* ... or this        */
  DEBUG_DB( sc->fCB, MyDB,Se_CC, "%d '%s'", sc->fID,sessionName );
  
  *sContext= (CContext)sc; /* return the created context structure */
  return LOCERR_OK;
} /* Session_CreateContext */



/* ----- "script-like" ADAPT --------- */
extern "C"
TSyError SyncEvolution_Session_AdaptItem( CContext sContext, appCharP *sItemData1, 
                                          appCharP *sItemData2,
                                          appCharP *sLocalVars, 
                                          uInt32  sIdentifier ) 
{ 
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,"Session_AdaptItem", "'%s' '%s' '%s' id=%d", 
                                *sItemData1,*sItemData2,*sLocalVars, sIdentifier );
  return LOCERR_OK;
} /* Session_AdaptItem */



/* Check the database entry of <deviceID> and return its nonce string */
extern "C"
TSyError SyncEvolution_Session_CheckDevice( CContext sContext,
                                            cAppCharP aDeviceID, appCharP *sDevKey,
                                            appCharP *nonce )
{
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  
  *sDevKey= StrAlloc( aDeviceID  );
  *nonce  = StrAlloc( "xyz_last" );
  DEBUG_DB( sc->fCB, MyDB,Se_CD, "%d dev='%s' nonce='%s'", sc->fID, *sDevKey,*nonce );
  return LOCERR_OK;
} /* Session_CheckDevice */



/* Get a new nonce from the database. If this returns an error, the SyncML engine
 * will create its own nonce.
 */
extern "C"
TSyError SyncEvolution_Session_GetNonce( CContext sContext, appCharP *nonce )
{
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_GN, "%d (not supported)", sc->fID );
  *nonce= NULL;
  return DB_NotFound;
} /* Session_GetNonce */

 
 
/* Save the new nonce (which will be expected to be returned 
 * in the next session for this device
 */
extern "C"
TSyError SyncEvolution_Session_SaveNonce( CContext sContext, cAppCharP nonce )
{
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_SN, "%d nonce='%s'", sc->fID, nonce );
  return LOCERR_OK;
} /* Session_SaveNonce */



/* Save the device info of <sContext> */
extern "C"
TSyError SyncEvolution_Session_SaveDeviceInfo( CContext sContext, cAppCharP aDeviceInfo )
{
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_SD, "%d info='%s'", sc->fID, aDeviceInfo );
  return LOCERR_OK;
} /* Session_SaveDeviceInfo */



/* Get the plugin's DB time */
extern "C"
TSyError SyncEvolution_Session_GetDBTime( CContext sContext, appCharP *currentDBTime )
{ 
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_GT, "%d", sc->fID );
  *currentDBTime= NULL;
  return DB_NotFound;
} /* Session_GetDBTime */



/* Return: Password_ClrText_IN    'SessionLogin' will get    clear text password
 *         Password_ClrText_OUT         "        must return clear text password
 *         Password_MD5_OUT             "        must return MD5 coded  password
 *         Password_MD5_Nonce_IN        "        will get    MD5B64(MD5B64(user:pwd):nonce)
 */
extern "C"
sInt32 SyncEvolution_Session_PasswordMode( CContext sContext )
{
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_PM, "%d mode=%d", sc->fID, sc->fPMode );
  return          sc->fPMode;
} /* Session_PasswordMode */



/* Make login */
extern "C"
TSyError SyncEvolution_Session_Login( CContext sContext, cAppCharP sUsername, appCharP *sPassword,
                                      appCharP *sUsrKey )
{ 
  /**** CAN BE ADAPTED BY USER ****/
  SessionContext* sc= SeC( sContext );
  TSyError       err= DB_Forbidden; /* default */
  
  /* different modes, choose one for the plugin */
  if (sc->fPMode==Password_ClrText_IN) {
    if (strcmp(  sUsername,"super" )==0 &&
        strcmp( *sPassword,"user"  )==0) { *sUsrKey  = StrAlloc( "1234" ); err= LOCERR_OK; }
  }
  else {       /* Password will be returned */
    if (strcmp(  sUsername,"super" )==0) { *sPassword= StrAlloc( "user" );
                                           *sUsrKey  = StrAlloc( "1234" ); err= LOCERR_OK; }
  } /* if */

  if (err) { DEBUG_DB( sc->fCB, MyDB,Se_LI, "%d usr='%s' err=%d",
                                             sc->fID,sUsername, err ); }
  else       DEBUG_DB( sc->fCB, MyDB,Se_LI, "%d usr='%s' pwd='%s' => key='%s'", 
                                             sc->fID,sUsername,*sPassword, *sUsrKey );
  return err;
} /* Session_Login */



/* Make logout */
extern "C"
TSyError SyncEvolution_Session_Logout( CContext sContext )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_LO, "%d",sc->fID );
  return LOCERR_OK;
} /* Session_Logout */



extern "C"
void SyncEvolution_Session_DisposeObj( CContext sContext, void* memory )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext*  sc= SeC( sContext );
  DEBUG_Exotic_DB( sc->fCB, MyDB,Se_DO, "%d free at %08X '%s'",
                   sc->fID, memory,memory );
  StrDispose              ( memory );
} /* Session_DisposeObj */



/* Can be implemented empty, if no action is required */
extern "C"
void SyncEvolution_Session_ThreadMayChangeNow( CContext sContext )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext*  sc= SeC( sContext );
  DEBUG_Exotic_DB( sc->fCB, MyDB,Se_TC, "%d",sc->fID );
} /* Session_ThreadMayChangeNow */



/* This routine is implemented for debug purposes only and will NOT BE CALLED by the
 * SyncML engine. Can be implemented empty, if not needed
 */
extern "C"
void SyncEvolution_Session_DispItems( CContext sContext, bool allFields, cAppCharP specificItem )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_DI, "%d %d '%s'",
                  sc->fID, allFields,specificItem );
} /* Session_DispItems */



/* Delete a session context */
extern "C"
TSyError SyncEvolution_Session_DeleteContext( CContext sContext ) 
{ 
  /**** CAN BE ADAPTED BY USER ****/ 
  SessionContext* sc= SeC( sContext );
  DEBUG_DB      ( sc->fCB, MyDB,Se_DC, "%d",sc->fID );

  delete sc;

  return LOCERR_OK;
} /* Session_DeleteContext */




/* ----------------------------------------------------------------- */
/* This is an example, how a context could be structured */
/* <aContext> will be casted to the SyncSource structure */
static SyncSource *DBC( CContext aContext ) { return (SyncSource *)aContext; }



/* -- OPEN ----------------------------------------------------------------------- */
/**
 * looks up datasource and uses pointer to it as context
 *
 * @param aContextName   name of previously instantiated SyncSource
 * @retval aContext      the corresponding SyncSource
 */
extern "C"
TSyError SyncEvolution_CreateContext( CContext *aContext, cAppCharP aContextName, DB_Callback aCB,
                                      cAppCharP sDevKey,
                                      cAppCharP sUsrKey )
{
    TSyError err = LOCERR_WRONGUSAGE;
    SyncSource *source = SyncContext::findSource(aContextName);
    if (source) {
        source->pushSynthesisAPI(aCB);
        *aContext = (CContext)source;
        err = LOCERR_OK;
    }
    DEBUG_DB( aCB, MyDB,Da_CC, "'%s' dev='%s' usr='%s'",
              aContextName, sDevKey, sUsrKey ); 
    return err;
}



extern "C"
uInt32 SyncEvolution_ContextSupport( CContext aContext, cAppCharP aContextRules ) 
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_CS, "%s '%s'", source->getName(),aContextRules );
    return 0;
}



extern "C"
uInt32 SyncEvolution_FilterSupport( CContext aContext, cAppCharP aFilterRules )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_FS, "%s '%s'", source->getName(),aFilterRules );
  return 0;
} /* FilterSupport */



/* -- ADMINISTRATION ------------------------------------------------------------ */
extern "C"
TSyError SyncEvolution_LoadAdminData( CContext aContext, cAppCharP aLocDB,
                                      cAppCharP aRemDB, appCharP *adminData )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_LA, "%s '%s' '%s'",
            source->getName(), aLocDB, aRemDB );
  *adminData= NULL;
  return DB_Forbidden; /* not yet implemented */
} /* LoadAdminData */



extern "C"
TSyError SyncEvolution_SaveAdminData( CContext aContext, cAppCharP adminData )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_SA, "%s '%s'", source->getName(), adminData );
  return DB_Forbidden; /* not yet implemented */
} /* SaveAdminData */



extern "C"
bool SyncEvolution_ReadNextMapItem( CContext aContext, MapID mID, bool aFirst )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_RM, "%s %08X first=%d (EOF)", source->getName(), mID, aFirst );
  return false; /* not yet implemented */
} /* ReadNextMapItem */



extern "C"
TSyError SyncEvolution_InsertMapItem( CContext aContext, cMapID mID )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_IM, "%s %08X: '%s' '%s' %04X %d", 
                                  source->getName(), mID, mID->localID, mID->remoteID, mID->flags, mID->ident );
  return DB_Forbidden; /* not yet implemented */
} /* InsertMapItem */



extern "C"
TSyError SyncEvolution_UpdateMapItem( CContext aContext, cMapID mID )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_UM, "%s %08X: '%s' '%s' %04X %d", 
                                  source->getName(), mID, mID->localID, mID->remoteID, mID->flags, mID->ident );
  return DB_Forbidden; /* not yet implemented */
} /* UpdateMapItem */



extern "C"
TSyError SyncEvolution_DeleteMapItem( CContext aContext, cMapID mID )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_DM, "%s %08X: '%s' '%s' %04X %d",
                                  source->getName(), mID, mID->localID, mID->remoteID, mID->flags, mID->ident );
  return DB_Forbidden; /* not yet implemented */
} /* DeleteMapItem */




/* -- GENERAL -------------------------------------------------------------------- */
extern "C"
void SyncEvolution_DisposeObj( CContext aContext, void* memory )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_Exotic_DB( source->getSynthesisAPI(), MyDB,Da_DO, "%s free at %08X", source->getName(),memory );
  free( memory );
} /* DisposeObj */



extern "C"
void SyncEvolution_ThreadMayChangeNow( CContext aContext )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  /* can be implemented empty, if no action is required */
  SyncSource *source = DBC( aContext );
  DEBUG_Exotic_DB( source->getSynthesisAPI(), MyDB,Da_TC, "%s", source->getName() );
} /* ThreadMayChangeNow */



extern "C"
void SyncEvolution_WriteLogData( CContext aContext, cAppCharP logData )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB, Da_WL, "%s (BEGIN)\n%s", source->getName(), logData );
  DEBUG_DB( source->getSynthesisAPI(), MyDB, Da_WL, "%s (END)",       source->getName() );
} /* WriteLogData */



/* This routine is implemented for debug purposes only and will NOT BE CALLED by the
 * SyncML engine. Can be implemented empty, if not needed
 */
extern "C"
void SyncEvolution_DispItems( CContext aContext, bool allFields, cAppCharP specificItem )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_DI, "%s %d '%s'", source->getName(),allFields,specificItem );
} /* DispItems */



/* ----- "script-like" ADAPT --------- */
extern "C"
TSyError SyncEvolution_AdaptItem( CContext aContext, appCharP *aItemData1,
                                  appCharP *aItemData2,
                                  appCharP *aLocalVars, 
                                  uInt32  aIdentifier ) 
{ 
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,"AdaptItem", "'%s' '%s' '%s' id=%d", 
                          *aItemData1,*aItemData2,*aLocalVars, aIdentifier );
  return LOCERR_OK;
} /* AdaptItem */



/* -- READ ---------------------------------------------------------------------- */
/**
 * Start data access here and complete it in SyncEvolution_EndDataWrite().
 */
extern "C"
TSyError SyncEvolution_StartDataRead( CContext aContext, cAppCharP   lastToken,
                                      cAppCharP resumeToken )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_SR, "%s last='%s' resume='%s'",
              source->getName(), lastToken,resumeToken );

    TSyError res = LOCERR_OK;
    try {
        if (source->getOperations().m_startDataRead) {
            res = source->getOperations().m_startDataRead(lastToken, resumeToken);
        }
        BOOST_FOREACH(const SyncSource::Operations::CallbackFunctor_t &callback,
                      source->getOperations().m_startSession) {
            callback();
        }
    } catch (...) {
        res = source->handleException();
    }

    return res;
}


extern "C"
TSyError SyncEvolution_ReadNextItemAsKey( CContext aContext, ItemID aID, KeyH aItemKey,
                                          sInt32*    aStatus, bool aFirst )
{
    /**** CAN BE ADAPTED BY USER ****/
    SyncSource *source = DBC( aContext );

    TSyError res = LOCERR_OK;
    *aStatus = 0;
    memset(aID, 0, sizeof(*aID));
    if (source->getOperations().m_readNextItem) {
        try {
            res = source->getOperations().m_readNextItem(aID, aStatus, aFirst);
        } catch (...) {
            res = source->handleException();
        }
    }

    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_RNK, "%s aStatus=%d aItemKey=%08X aID=(%s,%s)",
              source->getName(), *aStatus, aItemKey, aID->item, aID->parent );
    return res;
}

extern "C"
TSyError SyncEvolution_ReadItemAsKey( CContext aContext, cItemID aID, KeyH aItemKey )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_RIK, "%s aItemKey=%08X aID=(%s,%s)",
              source->getName(), aItemKey,  aID->item,aID->parent );

    TSyError res = LOCERR_OK;
    if (source->getOperations().m_readItemAsKey) {
        try {
            res = source->getOperations().m_readItemAsKey(aID, aItemKey);
        } catch (...) {
            res = source->handleException();
        }
    }

    return res;
}

#if 0
extern "C"
TSyError SyncEvolution_ReadBlob( CContext aContext, cItemID  aID,  cAppCharP  aBlobID,
                                 appPointer *aBlkPtr, uInt32 *aBlkSize, 
                                 uInt32 *aTotSize,
                                 bool  aFirst,    bool *aLast )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  const int sz= sizeof(int);
  
  int* ip = (int*)malloc( sz ); /* example BLOB structure for test (=4 bytes) */ 
      *ip = 231;

  *aBlkPtr = (appPointer)ip; if (*aBlkSize==0 || *aBlkSize>=sz) *aBlkSize= sz;
  *aTotSize= *aBlkSize;
  *aLast   = true;

  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_RB, "%s aID=(%s,%s) aBlobID=(%s)",
                                  source->getName(), aID->item,aID->parent, aBlobID );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,"",    "aBlkPtr=%08X aBlkSize=%d aTotSize=%d aFirst=%s aLast=%s", 
                                 *aBlkPtr, *aBlkSize, *aTotSize,
                                  aFirst?"true":"false", *aLast?"true":"false" );
  return LOCERR_OK;
} /* ReadBlob */
#endif


extern "C"
TSyError SyncEvolution_EndDataRead( CContext aContext )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_ER, "%s", source->getName() );

    TSyError res = LOCERR_OK;
    if (source->getOperations().m_endDataRead) {
        try {
            res = source->getOperations().m_endDataRead();
        } catch (...) {
            res = source->handleException();
        }
    }

    return res;
}




/* -- WRITE --------------------------------------------------------------------- */
extern "C"
TSyError SyncEvolution_StartDataWrite( CContext aContext )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_SW, "%s", source->getName() );
    return LOCERR_OK;
}

extern "C"
TSyError SyncEvolution_InsertItemAsKey( CContext aContext, KeyH aItemKey, ItemID newID )
{
    /**** CAN BE ADAPTED BY USER ****/
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_IIK, "%s %08X\n", source->getName(), aItemKey );

    TSyError res = LOCERR_OK;
    if (source->getOperations().m_insertItemAsKey) {
        try {
            res = source->getOperations().m_insertItemAsKey(aItemKey, newID);
        } catch (...) {
            res = source->handleException();
        }
    }

    return res;
}


extern "C"
TSyError SyncEvolution_UpdateItemAsKey( CContext aContext, KeyH aItemKey, cItemID   aID, 
                                        ItemID updID )
{
    SyncSource *source = DBC( aContext );
  
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_UI, "%s aID=(%s,%s)",
              source->getName(), aID->item,aID->parent );

    TSyError res = LOCERR_OK;
    if (source->getOperations().m_updateItemAsKey) {
        try {
            res = source->getOperations().m_updateItemAsKey(aItemKey, aID, updID);
        } catch (...) {
            res = source->handleException();
        }
    }

    return res;
}



extern "C"
TSyError SyncEvolution_MoveItem( CContext aContext, cItemID aID, cAppCharP newParID )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_MvI, "%s aID=(%s,%s) => (%s,%s)",
              source->getName(), aID->item,aID->parent,
              aID->item,newParID );
    return LOCERR_NOTIMP;
}



extern "C"
TSyError SyncEvolution_DeleteItem( CContext aContext, cItemID aID )
{
    SyncSource *source = DBC( aContext );
    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_DeI, "%s aID=(%s,%s)", source->getName(),aID->item,aID->parent );

    TSyError res = LOCERR_OK;
    if (source->getOperations().m_deleteItem) {
        try {
            res = source->getOperations().m_deleteItem (aID);
        } catch (...) {
            res = source->handleException();
        }
    }

    return res;
}



extern "C"
TSyError SyncEvolution_FinalizeLocalID( CContext aContext, cItemID aID, ItemID updID )
{
    return LOCERR_NOTIMP;
}



extern "C"
TSyError SyncEvolution_DeleteSyncSet( CContext aContext )
{
    return LOCERR_NOTIMP;
}


#if 0
extern "C"
TSyError SyncEvolution_WriteBlob( CContext aContext, cItemID aID,  cAppCharP aBlobID,
                                  appPointer aBlkPtr, uInt32 aBlkSize, 
                                  uInt32 aTotSize,
                                  bool aFirst,    bool aLast )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_WB, "%s aID=(%s,%s) aBlobID=(%s)",
                                  source->getName(), aID->item,aID->parent, aBlobID );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,"",    "aBlkPtr=%08X aBlkSize=%d aTotSize=%d aFirst=%s aLast=%s", 
                                  aBlkPtr, aBlkSize, aTotSize, 
                                  aFirst?"true":"false", aLast ?"true":"false" );
  return LOCERR_OK;
} /* WriteBlob */


extern "C"
TSyError SyncEvolution_DeleteBlob( CContext aContext, cItemID aID, cAppCharP aBlobID )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_DB, "%s aID=(%s,%s) aBlobID=(%s)",
                                  source->getName(), aID->item,aID->parent, aBlobID );
  return LOCERR_OK;
} /* DeleteBlob */

#endif


extern "C"
TSyError SyncEvolution_EndDataWrite( CContext aContext, bool success, appCharP *newToken )
{
    SyncSource *source = DBC( aContext );
    TSyError res = LOCERR_OK;
    try {
        BOOST_FOREACH(const SyncSource::Operations::CallbackFunctor_t &callback,
                      source->getOperations().m_endSession) {
            callback();
        }
        if (source->getOperations().m_endDataWrite) {
            res = source->getOperations().m_endDataWrite(success, newToken);
        }
    } catch (...) {
        res = source->handleException();
    }

    DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_EW, "%s %s '%s'", 
              source->getName(), success ? "COMMIT":"ROLLBACK", *newToken );
    return res;
}



/* ----------------------------------- */
extern "C"
TSyError SyncEvolution_DeleteContext( CContext aContext )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  DEBUG_DB( source->getSynthesisAPI(), MyDB,Da_DC, "%s", source->getName() );
  source->popSynthesisAPI();
  return LOCERR_OK;
}

SE_END_CXX
