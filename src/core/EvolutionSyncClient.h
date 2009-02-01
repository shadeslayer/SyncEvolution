/*
 * Copyright (C) 2005-2008 Patrick Ohly
 */

#ifndef INCL_EVOLUTIONSYNCCLIENT
#define INCL_EVOLUTIONSYNCCLIENT

#include <config.h>

#include "EvolutionSmartPtr.h"
#include "SyncEvolutionConfig.h"
#include <client/SyncClient.h>
#include <spds/SyncManagerConfig.h>

#include <string>
#include <set>
#include <stdint.h>
using namespace std;

class SourceList;
class EvolutionSyncSource;
namespace sysync {
    class TEngineModuleBridge;
    enum TEngineProgressEventType {
    };
};

/**
 * This is the main class inside SyncEvolution which
 * looks at the configuration, activates all enabled
 * sources and executes the synchronization.
 *
 * All interaction with the user (reporting progress, asking for
 * passwords, ...) is done via virtual methods. The default
 * implementation of those uses stdin/out.
 *
 */
class EvolutionSyncClient : public SyncClient, public EvolutionSyncConfig, public ConfigUserInterface {
    const string m_server;
    const set<string> m_sources;
    const bool m_doLogging;
    SyncMode m_syncMode;
    bool m_quiet;

    /**
     * a pointer to the active SourceList instance if one exists; 
     * used for error handling in throwError() on the iPhone
     */
    static SourceList *m_sourceListPtr;

    /**
     * Connection to the Synthesis engine. Always valid in a
     * constructed EvolutionSyncClient. Use getEngine() to reference
     * it.
     */
    sysync::TEngineModuleBridge *m_engine;

  public:
    /**
     * @param server     identifies the server config to be used
     * @param syncMode   setting this overrides the sync mode from the config
     * @param doLogging  write additional log and datatbase files about the sync
     */
    EvolutionSyncClient(const string &server,
                        bool doLogging = false,
                        const set<string> &sources = set<string>());
    ~EvolutionSyncClient();

    bool getQuiet() { return m_quiet; }
    void setQuiet(bool quiet) { m_quiet = quiet; }
    SyncMode getSyncMode() { return m_syncMode; }
    void setSyncMode(SyncMode syncMode) { m_syncMode = syncMode; }

    /**
     * Executes the sync, throws an exception in case of failure.
     * Handles automatic backups and report generation.
     */
    int sync();

    /**
     * Determines the log directory of the previous sync (either in
     * temp or logdir) and shows changes since then.
     */
    void status();

    /**
     * throws a runtime_error with the given string
     * or (on the iPhone, where exception handling is not
     * supported by the toolchain) prints an error directly
     * and aborts
     *
     * output format: <error>
     *
     * @param error     a string describing the error
     */
    static void throwError(const string &error);

    /**
     * throw an exception after an operation failed and
     * remember that this instance has failed
     *
     * output format: <action>: <error string>
     *
     * @Param action   a string describing the operation or object involved
     * @param error    the errno error code for the failure
     */
    static void throwError(const string &action, int error);

    /**
     * An error handler which prints the error message and then
     * stops the program. Never returns.
     *
     * The API was chosen so that it can be used as libebook/libecal
     * "backend-dies" signal handler.
     */
    static void fatalError(void *object, const char *error);

    /**
     * When using Evolution this function starts a background thread
     * which drives the default event loop. Without that loop
     * "backend-died" signals are not delivered. The problem with
     * the thread is that it seems to interfere with gconf startup
     * when added to the main() function of syncevolution. Therefore
     * it is started by EvolutionSyncSource::beginSync() (for unit
     * testing of sync sources) and EvolutionSyncClient::sync() (for
     * normal operation).
     */
    static void startLoopThread();


    /* AbstractSyncConfig API */
    virtual AbstractSyncSourceConfig* getAbstractSyncSourceConfig(const char* name) const;
    virtual AbstractSyncSourceConfig* getAbstractSyncSourceConfig(unsigned int i) const;
    virtual unsigned int getAbstractSyncSourceConfigsCount() const;

    /**
     * intercept config filters
     *
     * This call removes the "sync" source property and remembers
     * it separately because it has to be applied to only the active
     * sync sources; the generic config handling code would apply
     * it to all sources.
     */
    virtual void setConfigFilter(bool sync, const FilterConfigNode::ConfigFilter &filter);

  protected:

    sysync::TEngineModuleBridge &getEngine() { return *m_engine; }
    const sysync::TEngineModuleBridge &getEngine() const { return *m_engine; }

    /**
     * A helper function which interactively asks the user for
     * a certain password. May throw errors.
     *
     * The default implementation uses stdin/stdout to communicate
     * with the user.
     *
     * @param descr     A simple string explaining what the password is needed for,
     *                  e.g. "SyncML server". Has to be unique and understandable
     *                  by the user.
     * @return entered password
     */
    virtual string askPassword(const string &descr);

    /**
     * Callback for derived classes: called after setting up the client's
     * and sources' configuration. Can be used to reconfigure before
     * actually starting the synchronization.
     *
     * @param sources   a NULL terminated array of all active sources
     */
    virtual void prepare(SyncSource **sources);

    /**
     * display a text message from the server
     *
     * Not really used by SyncML servers. Could be displayed in a
     * modal dialog.
     *
     * @param message     string with local encoding, possibly with line breaks
     */
    virtual void displayServerMessage(const string &message);

    /**
     * display general sync session progress
     *
     * @param type    PEV_*, see "synthesis/engine_defs.h"
     * @param extra1  extra information depending on type
     * @param extra2  extra information depending on type
     * @param extra3  extra information depending on type
     */
    virtual void displaySyncProgress(sysync::TEngineProgressEventType type,
                                     int32_t extra1, int32_t extra2, int32_t extra3);

    /**
     * display sync source specific progress
     *
     * @param type    PEV_*, see "synthesis/engine_defs.h"
     * @param source  source which is the target of the event
     * @param extra1  extra information depending on type
     * @param extra2  extra information depending on type
     * @param extra3  extra information depending on type
     */
    virtual void displaySourceProgress(sysync::TEngineProgressEventType type,
                                       EvolutionSyncSource &source,
                                       int32_t extra1, int32_t extra2, int32_t extra3);

 private:
    /**
     * the code common to init() and status():
     * populate source list with active sources and open
     * them for reading without changing their state yet
     */
    void initSources(SourceList &sourceList);

    /**
     * sets up Synthesis session and executes it
     */
    void doSync();

    /**
     * override sync mode of all active sync sources if set
     */
    string m_overrideMode;
};

#endif // INCL_EVOLUTIONSYNCCLIENT
