#ifndef OPEN_NAVI_CLUSTER_AGENT_H
#define OPEN_NAVI_CLUSTER_AGENT_H

#include <open_navicore/cluster.h>

/*
 * Open NaviCore as a client of the cluster agent (cagentd). On by default (vp4r layer rule):
 * while the agent's socket exists the semantic cluster state is sent to the agent, which
 * arbitrates between navigation sources (Android Auto) and owns the cluster output; the provider
 * then does not send the PAS record itself. Without a running agent the provider publishes
 * locally as before. $VP4R_HOME/flags/navicore.d/no-cluster-agent switches the agent mode off.
 * The send is a non-blocking datagram: it never stalls the navigation thread, and a missing agent
 * only means the update is dropped (reported as failure).
 */

/* The mode is not switched off by the flag (the relay host is served). */
int open_navi_cluster_agent_enabled(void);

/* Enabled and the agent is running. Re-checked at most every few seconds and on reset. */
int open_navi_cluster_agent_active(void);

/* Send the state (already validated by the caller). Returns 0 when the datagram was handed to the
 * kernel, nonzero when it was dropped. */
uint32_t open_navi_cluster_agent_publish(const OpenNaviClusterNavigationState *state);

/* Tell the agent this source has nothing to show. */
void open_navi_cluster_agent_release(void);

/* The local PAS publication (validation, change masks, history, PAS callback) without the agent
 * redirect. Used by the relay host below, and equal to open_navi_cluster_backend_publish when the
 * agent mode is off. */
uint32_t open_navi_cluster_backend_publish_pas(const OpenNaviClusterNavigationState *state);

/*
 * Relay host: in agent mode the agent's output backend ("relay") sends the arbitrated state to
 * this process, because the PAS path (adapter 17 slot 3 inside nv_navigation) lives here. A thread
 * serves the socket and publishes through the local PAS path; an agent RELEASE (nothing to show)
 * publishes an empty state so the cluster drops its turn view. Started at provider
 * initialization unless the mode is switched off, stopped at deinitialization.
 */
void open_navi_cluster_relay_start(void);
void open_navi_cluster_relay_stop(void);

#if defined(OPEN_NAVI_TESTING)
void open_navi_cluster_relay_test_configure(const char *socket_path);
/* Override the off-flag path and agent socket path (NULL restores the defaults) and force a re-read. */
void open_navi_cluster_agent_test_configure(const char *off_flag_path, const char *socket_path);
#endif

#endif
