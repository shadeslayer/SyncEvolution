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
 * @param mContextName   name of previously instantiated SyncSource, "" when used as session module
 * @retval mContext      the corresponding SyncSource
 */
extern "C"
TSyError SyncEvolution_Module_CreateContext( CContext *mContext, cAppCharP   moduleName,
                                             cAppCharP      subName,
                                             cAppCharP mContextName,
                                             DB_Callback mCB )
{
    TSyError err = LOCERR_WRONGUSAGE;
    if (!mContextName[0]) {
        *mContext = NULL;
        err = LOCERR_OK;
    } else {
        SyncSource *source = SyncContext::findSource(mContextName);
        if (source) {
            source->pushSynthesisAPI(mCB);
            *mContext = (CContext)source;
            err = LOCERR_OK;
        }
    }

    SE_LOG_DEBUG(NULL, NULL, "CreateContext %s/%s/%s => %d",
                 moduleName, subName, mContextName, err);
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
        SE_LOG_DEBUG(NULL, NULL, "Module_Version = %08lx", (long)v);
    }
    return v;
}



/* Get the plug-in's capabilities */
extern "C"
TSyError SyncEvolution_Module_Capabilities( CContext mContext, appCharP *mCapabilities )
{
    SyncSource *source  = MoC(mContext);

    std::stringstream s;
    s << MyPlatform() << "\n"
      << DLL_Info << "\n"
      << CA_MinVersion << ":V1.0.6.0\n" /* must not be changed */
      << CA_Manufacturer << ":SyncEvolution\n"
      << CA_Description << ":SyncEvolution Synthesis DB Plugin\n"
      << Plugin_DS_Data_Str << ":no\n"
      << Plugin_DS_Data_Key << ":yes\n"
      << CA_ItemAsKey << ":yes\n"
      << Plugin_DS_Blob <<
        ((source && source->getOperations().m_readBlob) ?
         ":yes\n" :
         ":no\n");

    if (source && source->getOperations().m_loadAdminData) {
        s << Plugin_DS_Admin << ":yes\n";
    }

    *mCapabilities= StrAlloc(s.str().c_str());
    SE_LOG_DEBUG(NULL, NULL, "Module_Capabilities:\n%s", *mCapabilities);
    return LOCERR_OK;
} /* Module_Capabilities */



extern "C"
TSyError SyncEvolution_Module_PluginParams( CContext mContext,
                                            cAppCharP mConfigParams, CVersion engineVersion )
{
    SyncSource *source  = MoC(mContext);
    SE_LOG_DEBUG(source, NULL, "Module_PluginParams\n Engine=%08lX\n %s",
                 (long)engineVersion, mConfigParams);
    /*return LOCERR_CFGPARSE;*/ /* if there are unsupported params */
    return LOCERR_OK;
} /* Module_PluginParams */



/* Dispose the memory of the module context */
extern "C"
void SyncEvolution_Module_DisposeObj( CContext mContext, void* memory )
{
    StrDispose(memory);
}

extern "C"
TSyError SyncEvolution_Module_DeleteContext( CContext mContext )
{
    SyncSource  *source = MoC(mContext);
    SE_LOG_DEBUG(NULL, NULL, "Module_DeleteContext %s",
                 source ? source->getName().c_str() : "'session'");
    if (source) {
        source->popSynthesisAPI();
    }
    return LOCERR_OK;
}


/* <sContext> will be casted to the SyncContext* structure */
static SyncContext* SeC( CContext sContext ) { return (SyncContext*)sContext; }



/* Create a context for a new session. Maps to the existing SyncContext. */ 
extern "C"
TSyError SyncEvolution_Session_CreateContext( CContext *sContext, cAppCharP sessionName, DB_Callback sCB )
{ 
    *sContext = (CContext)SyncContext::findContext(sessionName);
    SE_LOG_DEBUG(NULL, NULL, "Session_CreateContext '%s' %s", 
                 sessionName, *sContext ? "found" : "not found");
    if (*sContext) {
        return LOCERR_OK;
    } else {
        return DB_NotFound;
    }
} /* Session_CreateContext */



/* ----- "script-like" ADAPT --------- */
extern "C"
TSyError SyncEvolution_Session_AdaptItem( CContext sContext, appCharP *sItemData1, 
                                          appCharP *sItemData2,
                                          appCharP *sLocalVars, 
                                          uInt32  sIdentifier ) 
{ 
    /**** CAN BE ADAPTED BY USER ****/ 
    // SyncContext* sc= SeC( sContext );
    SE_LOG_DEBUG(NULL, NULL, "Session_AdaptItem '%s' '%s' '%s' id=%d", 
                 *sItemData1,*sItemData2,*sLocalVars, sIdentifier);
    return LOCERR_OK;
} /* Session_AdaptItem */



/* Check the database entry of <deviceID> and return its nonce string */
extern "C"
TSyError SyncEvolution_Session_CheckDevice( CContext sContext,
                                            cAppCharP aDeviceID, appCharP *sDevKey,
                                            appCharP *nonce )
{
    SyncContext *sc = SeC(sContext);
    if (!sc) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = LOCERR_OK;

    sc->setSyncDeviceID(aDeviceID);
    string id = sc->getRemoteDevID();
    if (id.empty()) {
        sc->setRemoteDevID(aDeviceID);
        sc->flush();
    } else if (id != aDeviceID) {
        // We are using the wrong configuration?! Refuse to continue.
        SE_LOG_ERROR(NULL, NULL, "remote device ID '%s' in config does not match the one from the peer '%s' - incorrect configuration?!",
                     id.c_str(), aDeviceID);
        res = DB_Forbidden;
    }

    *sDevKey= StrAlloc(aDeviceID);
    *nonce = StrAlloc(sc->getNonce().c_str());
    SE_LOG_DEBUG(NULL, NULL, "Session_CheckDevice dev='%s' nonce='%s' res=%d",
                 *sDevKey, *nonce, res);
    return res;
} /* Session_CheckDevice */



/* Get a new nonce from the database. If this returns an error, the SyncML engine
 * will create its own nonce.
 */
extern "C"
TSyError SyncEvolution_Session_GetNonce( CContext sContext, appCharP *nonce )
{
    return DB_NotFound;
} /* Session_GetNonce */

 
 
/* Save the new nonce (which will be expected to be returned 
 * in the next session for this device
 */
extern "C"
TSyError SyncEvolution_Session_SaveNonce( CContext sContext, cAppCharP nonce )
{
    SyncContext *sc = SeC(sContext);
    if (!sc) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(NULL, NULL, "Session_SaveNonce nonce='%s'",
                 nonce);
    sc->setNonce(nonce);
    sc->flush();
    return LOCERR_OK;
} /* Session_SaveNonce */



/* Save the device info of <sContext> */
extern "C"
TSyError SyncEvolution_Session_SaveDeviceInfo( CContext sContext, cAppCharP aDeviceInfo )
{
    SyncContext *sc = SeC(sContext);
    if (!sc) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(NULL, NULL, "Session_SaveDeviceInfo info='%s'",
                 aDeviceInfo );
    sc->setDeviceData(aDeviceInfo);
    sc->flush();
    return LOCERR_OK;
} /* Session_SaveDeviceInfo */



/* Get the plugin's DB time */
extern "C"
TSyError SyncEvolution_Session_GetDBTime( CContext sContext, appCharP *currentDBTime )
{ 
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
    return Password_ClrText_OUT;
} /* Session_PasswordMode */



/* Make login */
extern "C"
TSyError SyncEvolution_Session_Login( CContext sContext, cAppCharP sUsername, appCharP *sPassword,
                                      appCharP *sUsrKey )
{ 
    SyncContext *sc = SeC(sContext);
    if (!sc) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = DB_Forbidden;
    string user = sc->getSyncUsername();
    string password = sc->getSyncPassword();

    if (user.empty() && password.empty()) {
        // nothing to check, accept peer
        res = LOCERR_OK;
    } else if (user == sUsername) {
        *sPassword=StrAlloc(password.c_str());
        res = LOCERR_OK;
    }

    SE_LOG_DEBUG(NULL, NULL, "Session_Login usr='%s' expected user='%s' res=%d",
                 sUsername, user.c_str(), res);
    return res;
} /* Session_Login */



/* Make logout */
extern "C"
TSyError SyncEvolution_Session_Logout( CContext sContext )
{
    return LOCERR_OK;
} /* Session_Logout */



extern "C"
void SyncEvolution_Session_DisposeObj( CContext sContext, void* memory )
{
  StrDispose              ( memory );
} /* Session_DisposeObj */



/* Can be implemented empty, if no action is required */
extern "C"
void SyncEvolution_Session_ThreadMayChangeNow( CContext sContext )
{
} /* Session_ThreadMayChangeNow */



/* This routine is implemented for debug purposes only and will NOT BE CALLED by the
 * SyncML engine. Can be implemented empty, if not needed
 */
extern "C"
void SyncEvolution_Session_DispItems( CContext sContext, bool allFields, cAppCharP specificItem )
{
} /* Session_DispItems */



/* Delete a session context */
extern "C"
TSyError SyncEvolution_Session_DeleteContext( CContext sContext ) 
{ 
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
    SE_LOG_DEBUG(source, NULL, "'%s' dev='%s' usr='%s' err=%d",
                 aContextName, sDevKey, sUsrKey, err);
    return err;
}



extern "C"
uInt32 SyncEvolution_ContextSupport( CContext aContext, cAppCharP aContextRules ) 
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(source, NULL, "ContextSupport %s", aContextRules);
    return 0;
}



extern "C"
uInt32 SyncEvolution_FilterSupport( CContext aContext, cAppCharP aFilterRules )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  if (!source) {
      return LOCERR_WRONGUSAGE;
  }
  SE_LOG_DEBUG(source, NULL, "FilterSupport %s", aFilterRules);
  return 0;
} /* FilterSupport */



/* -- ADMINISTRATION ------------------------------------------------------------ */
extern "C"
TSyError SyncEvolution_LoadAdminData( CContext aContext, cAppCharP aLocDB,
                                      cAppCharP aRemDB, appCharP *adminData )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_loadAdminData(*source, aLocDB, aRemDB, adminData);
    SE_LOG_DEBUG(source, NULL, "LoadAdminData '%s' '%s', '%s' res=%d",
                 aLocDB, aRemDB, *adminData ? *adminData : "", res);
    return res;
} /* LoadAdminData */



extern "C"
TSyError SyncEvolution_SaveAdminData( CContext aContext, cAppCharP adminData )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_saveAdminData(*source, adminData);
    SE_LOG_DEBUG(source, NULL, "SaveAdminData '%s' res=%d", adminData, res);
    return res;
} /* SaveAdminData */



extern "C"
bool SyncEvolution_ReadNextMapItem( CContext aContext, MapID mID, bool aFirst )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    bool res = false;
    try {
        // always reset mID, just in case that caller expects it or
        // operation doesn't do it correctly
        mID->localID = NULL;
        mID->remoteID = NULL;
        mID->ident = 0;
        mID->flags = 0;
        if (source->getOperations().m_readNextMapItem) {
            res = source->getOperations().m_readNextMapItem(mID, aFirst);
        }
    } catch (...) {
        res = source->handleException();
    }

    SE_LOG_DEBUG(source, NULL, "ReadNextMapItem '%s' + %x = '%s' + %d first=%s res=%d",
                 res ? NullPtrCheck(mID->localID) : "(none)",
                 res ? mID->ident : 0,
                 res ? NullPtrCheck(mID->remoteID) : "(none)",
                 res ? mID->flags : 0,
                 aFirst ? "yes" : "no",
                 res);
    return res;
} /* ReadNextMapItem */



extern "C"
TSyError SyncEvolution_InsertMapItem( CContext aContext, cMapID mID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_insertMapItem(*source, mID);
    SE_LOG_DEBUG(source, NULL, "InsertMapItem '%s' + %x = '%s' + %x res=%d", 
                 NullPtrCheck(mID->localID), mID->ident,
                 NullPtrCheck(mID->remoteID), mID->flags,
                 res);
    return res;
} /* InsertMapItem */



extern "C"
TSyError SyncEvolution_UpdateMapItem( CContext aContext, cMapID mID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_updateMapItem(*source, mID);
    SE_LOG_DEBUG(source, NULL, "UpdateMapItem '%s' + %x = '%s' + %x, res=%d", 
                 mID->localID, mID->ident,
                 mID->remoteID, mID->flags,
                 res);

    return res;
} /* UpdateMapItem */



extern "C"
TSyError SyncEvolution_DeleteMapItem( CContext aContext, cMapID mID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_deleteMapItem(*source, mID);
    SE_LOG_DEBUG(source, NULL, "DeleteMapItem '%s' + %x = '%s' + %x res=%d",
                 mID->localID, mID->ident,
                 mID->remoteID, mID->flags,
                 res);
    return res;
} /* DeleteMapItem */




/* -- GENERAL -------------------------------------------------------------------- */
extern "C"
void SyncEvolution_DisposeObj( CContext aContext, void* memory )
{
  free( memory );
} /* DisposeObj */



extern "C"
void SyncEvolution_ThreadMayChangeNow( CContext aContext )
{
} /* ThreadMayChangeNow */



extern "C"
void SyncEvolution_WriteLogData( CContext aContext, cAppCharP logData )
{
} /* WriteLogData */



/* This routine is implemented for debug purposes only and will NOT BE CALLED by the
 * SyncML engine. Can be implemented empty, if not needed
 */
extern "C"
void SyncEvolution_DispItems( CContext aContext, bool allFields, cAppCharP specificItem )
{
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
  if (!source) {
      return LOCERR_WRONGUSAGE;
  }
  SE_LOG_DEBUG(source, NULL, "AdaptItem '%s' '%s' '%s' id=%d", 
               *aItemData1, *aItemData2, *aLocalVars, aIdentifier);
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
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_startDataRead(*source, lastToken, resumeToken);
    SE_LOG_DEBUG(source, NULL, "StartDataRead last='%s' resume='%s' res=%d",
                 lastToken, resumeToken, res);
    return res;
}


extern "C"
TSyError SyncEvolution_ReadNextItemAsKey( CContext aContext, ItemID aID, KeyH aItemKey,
                                          sInt32*    aStatus, bool aFirst )
{
    /**** CAN BE ADAPTED BY USER ****/
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    *aStatus = 0;
    memset(aID, 0, sizeof(*aID));
    TSyError res = source->getOperations().m_readNextItem(*source, aID, aStatus, aFirst);
    SE_LOG_DEBUG(source, NULL, "ReadNextItemAsKey aStatus=%d aID=(%s,%s) res=%d",
                 *aStatus, aID->item, aID->parent, res);
    return res;
}

extern "C"
TSyError SyncEvolution_ReadItemAsKey( CContext aContext, cItemID aID, KeyH aItemKey )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_readItemAsKey(*source, aID, aItemKey);
    SE_LOG_DEBUG(source, NULL, "ReadItemAsKey aID=(%s,%s) res=%d",
                 aID->item, aID->parent, res);
    return res;
}

extern "C"
sysync::TSyError SyncEvolution_ReadBlob(CContext aContext, cItemID  aID,  cAppCharP  aBlobID,
                                        appPointer *aBlkPtr, memSize *aBlkSize, 
                                        memSize *aTotSize,
                                        bool  aFirst,    bool *aLast)
{
  SyncSource *source = DBC( aContext );
  if (!source) {
      return LOCERR_WRONGUSAGE;
  }

  TSyError res;
  if (source->getOperations().m_readBlob) {
      try {
	    size_t blksize, totsize;
	    /* Another conversion between memSize and size_t to make s390 happy */
            res = source->getOperations().m_readBlob(aID, aBlobID, (void **)aBlkPtr,
						     aBlkSize ? &blksize : NULL,
						     aTotSize ? &totsize : NULL,
						     aFirst, aLast);
	    if (aBlkSize) {
	        *aBlkSize = blksize;
	    }
            if (aTotSize) {
                *aTotSize = totsize;
            }

      } catch (...) {
          res = source->handleException();
      }
  } else {
      res = LOCERR_NOTIMP;
  }

  SE_LOG_DEBUG(source, NULL, "ReadBlob aID=(%s,%s) aBlobID=(%s) aBlkPtr=%p aBlkSize=%lu aTotSize=%lu aFirst=%s aLast=%s res=%d",
               aID->item,aID->parent, aBlobID, aBlkPtr, (unsigned long)*aBlkSize, (unsigned long)*aTotSize,
               aFirst? "true" : "false", *aLast ? "true" : "false", res);
  return res;
} /* ReadBlob */


extern "C"
TSyError SyncEvolution_EndDataRead( CContext aContext )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_endDataRead(*source);
    SE_LOG_DEBUG(source, NULL, "EndDataRead res=%d", res);
    return res;
}




/* -- WRITE --------------------------------------------------------------------- */
extern "C"
TSyError SyncEvolution_StartDataWrite( CContext aContext )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(source, NULL, "StartDataWrite");
    return LOCERR_OK;
}

extern "C"
TSyError SyncEvolution_InsertItemAsKey( CContext aContext, KeyH aItemKey, ItemID newID )
{
    /**** CAN BE ADAPTED BY USER ****/
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_insertItemAsKey(*source, aItemKey, newID);
    SE_LOG_DEBUG(source, NULL, "InsertItemAsKey res=%d\n", res);
    return res;
}


extern "C"
TSyError SyncEvolution_UpdateItemAsKey( CContext aContext, KeyH aItemKey, cItemID   aID, 
                                        ItemID updID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_updateItemAsKey(*source, aItemKey, aID, updID);
    SE_LOG_DEBUG(source, NULL, "aID=(%s,%s) res=%d",
                 aID->item,aID->parent, res);
    return res;
}



extern "C"
TSyError SyncEvolution_MoveItem( CContext aContext, cItemID aID, cAppCharP newParID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(source, NULL, "MoveItem aID=(%s,%s) => (%s,%s)",
                 aID->item,aID->parent, aID->item,newParID);
    return LOCERR_NOTIMP;
}



extern "C"
TSyError SyncEvolution_DeleteItem( CContext aContext, cItemID aID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_deleteItem(*source, aID);
    SE_LOG_DEBUG(source, NULL, "DeleteItem aID=(%s,%s) res=%d",
                 aID->item, aID->parent, res);
    return res;
}



extern "C"
TSyError SyncEvolution_FinalizeLocalID( CContext aContext, cItemID aID, ItemID updID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(source, NULL, "FinalizeLocalID not implemented");
    return LOCERR_NOTIMP;
}



extern "C"
TSyError SyncEvolution_DeleteSyncSet( CContext aContext )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    SE_LOG_DEBUG(source, NULL, "DeleteSyncSet not implemented");
    return LOCERR_NOTIMP;
}


extern "C"
TSyError SyncEvolution_WriteBlob(CContext aContext, cItemID aID,  cAppCharP aBlobID,
                                 appPointer aBlkPtr, memSize aBlkSize, 
                                 memSize aTotSize,
                                 bool aFirst,    bool aLast)
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }

    TSyError res;
    if (source->getOperations().m_writeBlob) {
        try {
            res = source->getOperations().m_writeBlob(aID, aBlobID, aBlkPtr, aBlkSize,
                                                      aTotSize, aFirst, aLast);
        } catch (...) {
            res = source->handleException();
        }
    } else {
        res = LOCERR_NOTIMP;
    }
    
    SE_LOG_DEBUG(source, NULL, "WriteBlob aID=(%s,%s) aBlobID=(%s) aBlkPtr=%p aBlkSize=%lu aTotSize=%lu aFirst=%s aLast=%s res=%d",
                 aID->item,aID->parent, aBlobID, aBlkPtr, (unsigned long)aBlkSize, (unsigned long)aTotSize, 
                 aFirst ? "true" : "false", aLast ? "true" : "false", res);
    return res;
} /* WriteBlob */


extern "C"
TSyError SyncEvolution_DeleteBlob( CContext aContext, cItemID aID, cAppCharP aBlobID )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }

    TSyError res = source->getOperations().m_deleteBlob(*source, aID, aBlobID);
    SE_LOG_DEBUG(source, NULL, "DeleteBlob aID=(%s,%s) aBlobID=(%s) res=%d",
                 aID->item,aID->parent, aBlobID, res);
    return res;
} /* DeleteBlob */

extern "C"
TSyError SyncEvolution_EndDataWrite( CContext aContext, bool success, appCharP *newToken )
{
    SyncSource *source = DBC( aContext );
    if (!source) {
        return LOCERR_WRONGUSAGE;
    }
    TSyError res = source->getOperations().m_endDataWrite(*source, success, newToken);
    SE_LOG_DEBUG(source, NULL, "EndDataWrite %s '%s' res=%d", 
                 success ? "COMMIT":"ROLLBACK", *newToken, res);
    return res;
}

/* ----------------------------------- */
extern "C"
TSyError SyncEvolution_DeleteContext( CContext aContext )
{
  /**** CAN BE ADAPTED BY USER ****/ 
  SyncSource *source = DBC( aContext );
  if (!source) {
      return LOCERR_WRONGUSAGE;
  }
  SE_LOG_DEBUG(source, NULL, "DeleteContext");
  source->popSynthesisAPI();
  return LOCERR_OK;
}

SE_END_CXX
