// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "tbv.h"

static void tbv_core_log_backend(enum tbv_backend_type type)
{
	const struct tbv_backend_ops *ops = tbv_backend_get(type);

	if (!ops) {
		pr_warn("missing backend ops for %s\n", tbv_backend_name(type));
		return;
	}

	pr_info("backend %s rc=%u uc=%u tbnet_identity=%u\n",
		ops->name, ops->supports_rc, ops->supports_uc,
		ops->needs_tbnet_identity);
}

int tbv_core_init(struct tbv_state *state,
		  const struct tbv_resolved_config *cfg,
		  const struct tbv_tbnet_identity_config *identity_cfg)
{
	int ret;

	memset(state, 0, sizeof(*state));
	state->cfg = *cfg;
	mutex_init(&state->lock);
	INIT_LIST_HEAD(&state->peers);
	xa_init(&state->verbs_mrs_xa);
	xa_init(&state->verbs_qps_xa);
	state->next_peer_id = 1;

	if (!cfg->native_enabled && !cfg->apple_enabled)
		return -EINVAL;

	ret = tbv_tbnet_identity_prepare(&state->tbnet_identity, cfg,
					 identity_cfg);
	if (ret) {
		mutex_destroy(&state->lock);
		return ret;
	}

	ret = tbv_debugfs_init(state);
	if (ret) {
		tbv_tbnet_identity_stop(&state->tbnet_identity);
		mutex_destroy(&state->lock);
		return ret;
	}

	if (cfg->native_enabled)
		tbv_core_log_backend(TBV_BACKEND_NATIVE);

	if (cfg->apple_enabled)
		tbv_core_log_backend(TBV_BACKEND_APPLE);

	pr_info("core ready profile=%s rc=%u uc=%u\n",
		tbv_profile_name(cfg->profile), cfg->rc_supported,
		cfg->uc_supported);
	return 0;
}

void tbv_core_exit(struct tbv_state *state)
{
	if (!list_empty(&state->peers))
		pr_warn("unloading with live peers after service teardown\n");

	if (state->verbs_parent) {
		put_device(state->verbs_parent);
		state->verbs_parent = NULL;
	}

	tbv_debugfs_exit(state);
	if (!xa_empty(&state->verbs_mrs_xa))
		pr_warn("unloading with live MR registry entries\n");
	if (!xa_empty(&state->verbs_qps_xa))
		pr_warn("unloading with live QP registry entries\n");
	xa_destroy(&state->verbs_mrs_xa);
	xa_destroy(&state->verbs_qps_xa);
	tbv_tbnet_identity_stop(&state->tbnet_identity);
	mutex_destroy(&state->lock);
}

void tbv_state_set_verbs_parent(struct tbv_state *state, struct device *dev)
{
	if (!dev)
		return;

	mutex_lock(&state->lock);
	if (!state->verbs_parent)
		state->verbs_parent = get_device(dev);
	mutex_unlock(&state->lock);
}

struct device *tbv_state_get_verbs_parent(struct tbv_state *state)
{
	struct device *dev;

	mutex_lock(&state->lock);
	dev = state->verbs_parent ? get_device(state->verbs_parent) : NULL;
	mutex_unlock(&state->lock);

	return dev;
}
