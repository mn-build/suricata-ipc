//
// Created by Danny Browning on 10/6/18.
//

#include "suricata-common.h"
#include "tm-threads.h"
#include "conf.h"
#include "runmodes.h"
#include "runmode-ipc.h"
#include "output.h"

#include "detect-engine.h"
#include "source-pcap-file.h"

#include "util-debug.h"
#include "util-time.h"
#include "util-cpu.h"
#include "util-affinity.h"

#include "util-runmodes.h"

const char *RunModeIpcGetDefaultMode(void)
{
    return "autofp";
}

void RunModeIpcRegister(int slot)
{
    RunModeRegisterNewRunMode(slot, "single",
                              "Single threaded ipc mode",
                              RunModeIpcSingle);
    RunModeRegisterNewRunMode(slot, "autofp",
                              "Multi threaded ipc mode.  Packets from "
                              "each flow are assigned to a single detect thread.",
                              RunModeIpcAutoFp);
    RunModeRegisterNewRunMode(slot, "workers",
                              "Ipc workers mode, each thread does all"
                              "tasks from acquisition to logging",
                              RunModeIpcWorkers);
    return;
}

static void IpcDerefConfig(void *conf)
{
    IpcConfig *ipc = (IpcConfig *)conf;
    if (SC_ATOMIC_SUB(ipc->ref, 1) == 1) {
        for(int i = 0; i < ipc->nb_servers; i++) {
            SCFree(ipc->servers[i]);
        }
        SCFree(ipc->servers);
        SCFree(ipc);
    }
}

static int IpcGetThreadsCount(void *conf)
{
    IpcConfig *ipc = (IpcConfig *)conf;
    return ipc->nb_servers;
}

static void *ParseIpcConfig(const char *servers)
{
    SCLogInfo("Ipc using servers %s", servers);

    IpcConfig *conf = SCMalloc(sizeof(IpcConfig));
    if(unlikely(conf == NULL)) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        return NULL;
    }
    memset(conf, 0, sizeof(IpcConfig));

    char delim[] = ",";

    // looping the list of servers twice because we're at startup and it's easier than using a list
    char * saveptr = NULL;
    char * token = strtok_r(servers, delim, &saveptr);
    conf->nb_servers = 0;
    while (token != NULL) {
        conf->nb_servers += 1;
        token = strtok_r(NULL, delim, &saveptr);
    }

    SCLogInfo("Connecting %d servers", conf->nb_servers);

    conf->servers = SCMalloc(sizeof(char*) * conf->nb_servers);
    if(unlikely(conf->servers == NULL)) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        return NULL;
    }

    int server = 0;
    saveptr = NULL;
    token = strtok_r(servers, delim, &saveptr);
    while (token != NULL) {
        conf->servers[server] = SCStrdup(token);
        if(unlikely(conf->servers[server] == NULL)) {
            SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
            return NULL;
        }
        server += 1;
        token = strtok_r(NULL, delim, &saveptr);
    }

    conf->allocation_batch = 100;
    if(ConfGetInt("ipc.allocation-batch", &conf->allocation_batch) == 0) {
        SCLogInfo("No ipc.allocation-batch parameters, defaulting to 100");
    }

    conf->DerefFunc = IpcDerefConfig;

    return conf;
}

/**
 * \brief RunModeIpcAutoFp set up the following thread packet handlers:
 *        - Receive thread (from ipc server)
 *        - Decode thread
 *        - Stream thread
 *        - Detect: If we have only 1 cpu, it will setup one Detect thread
 *                  If we have more than one, it will setup num_cpus - 1
 *                  starting from the second cpu available.
 *        - Outputs thread
 *        By default the threads will use the first cpu available
 *        except the Detection threads if we have more than one cpu.
 *
 * \retval 0 If all goes well. (If any problem is detected the engine will
 *           exit()).
 */
int RunModeIpcAutoFp(void)
{
    SCEnter();

    const char *server = NULL;
    if (ConfGet("ipc.server", &server) == 0) {
        SCLogError(SC_ERR_RUNMODE, "Failed retrieving ipc.server from Conf");
        exit(EXIT_FAILURE);
    }

    RunModeInitialize();

    TimeModeSetLive();

    int ret = RunModeSetLiveCaptureSingle(ParseIpcConfig,
                                      IpcGetThreadsCount,
                                      "ReceiveIpc",
                                      "DecodeIpc",
                                      thread_name_single,
                                      server);
    if (ret != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeIpcAutoFp initialised");

    return 0;
}

/**
 * \brief Single thread version of the Ipc runmode.
 */
int RunModeIpcSingle(void)
{
    SCEnter();

    const char *server = NULL;
    if (ConfGet("ipc.server", &server) == 0) {
        SCLogError(SC_ERR_RUNMODE, "Failed retrieving ipc.server from Conf");
        exit(EXIT_FAILURE);
    }

    RunModeInitialize();

    TimeModeSetLive();

    int ret = RunModeSetLiveCaptureAutoFp(ParseIpcConfig,
                                          IpcGetThreadsCount,
                                          "ReceiveIpc",
                                          "DecodeIpc",
                                          thread_name_autofp,
                                          server);
    if (ret != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeIpcSingle initialised");

    return 0;
}

/**
 * \brief Workers version of the Ipc runmode.
 */
int RunModeIpcWorkers(void)
{
    SCEnter();

    int ret;

    RunModeInitialize();

    TimeModeSetLive();

    const char *server = NULL;
    if (ConfGet("ipc.server", &server) == 0) {
        SCLogError(SC_ERR_RUNMODE, "Failed retrieving ipc.server from Conf");
        exit(EXIT_FAILURE);
    }

    RunModeInitialize();

    TimeModeSetLive();

    ret = RunModeSetLiveCaptureWorkers(ParseIpcConfig,
                                       IpcGetThreadsCount,
                                       "ReceiveIpc",
                                       "DecodeIpc",
                                       thread_name_workers,
                                       server);
    if (ret != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeIpcWorkers initialised");

    return ret;
}