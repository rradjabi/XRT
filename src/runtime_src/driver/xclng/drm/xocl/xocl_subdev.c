/*
 * Copyright (C) 2018 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/platform_device.h>
#include "xclfeatures.h"
#include "xocl_drv.h"
#include "version.h"

struct xocl_subdev_array {
	xdev_handle_t xdev_hdl;
	int id;
	struct platform_device **pldevs;
	int count;
};

static DEFINE_IDA(xocl_dev_minor_ida);
static DEFINE_IDA(subdev_inst_ida);

static struct xocl_dsa_vbnv_map dsa_vbnv_map[] = {
	XOCL_DSA_VBNV_MAP
};

void xocl_subdev_fini(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i;

	for (i = 0; i < XOCL_SUBDEV_NUM; i++) {
		if (core->subdevs[i]) {
			vfree(core->subdevs[i]);
			core->subdevs[i] = NULL;
		}
	}
}

int xocl_subdev_init(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i, ret = 0;

	for (i = 0; i < XOCL_SUBDEV_NUM; i++) {
		core->subdevs[i] = vzalloc(sizeof(struct xocl_subdev) *
				XOCL_SUBDEV_MAX_INST);
		if (!core->subdevs[i]) {
			ret = -ENOMEM;
			goto failed;
		}
	}

	return 0;

failed:
	xocl_subdev_fini(xdev_hdl);
	return ret;
}
static struct xocl_subdev *xocl_subdev_reserve(xdev_handle_t xdev_hdl,
		struct xocl_subdev_info *sdev_info)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct xocl_subdev *subdev;
	int devid = sdev_info->id;
	int max = sdev_info->multi_inst ? XOCL_SUBDEV_MAX_INST : 1;
	int i;

	mutex_lock(&core->lock);
	for (i = 0; i < max; i++) {
		subdev = &core->subdevs[devid][i];
		if (subdev->state == XOCL_SUBDEV_STATE_UNINIT) {
			subdev->state = XOCL_SUBDEV_STATE_INIT;
			break;
		}
	}
	mutex_unlock(&core->lock);
	if (i == max)
		return NULL;

	subdev->inst = ida_simple_get(&subdev_inst_ida,
			0, 0, GFP_KERNEL);
	if (subdev->inst < 0) {
		xocl_xdev_err(xdev_hdl, "Not enought inst id");
		goto error;
	}

	return subdev;

error:
	mutex_lock(&core->lock);
	subdev->state = XOCL_SUBDEV_STATE_UNINIT;
	mutex_unlock(&core->lock);

	return NULL;
}

static void xocl_subdev_destroy(xdev_handle_t xdev_hdl,
		struct xocl_subdev *subdev)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct platform_device *pldev;
	int state;

	mutex_lock(&core->lock);
	if (subdev->state == XOCL_SUBDEV_STATE_UNINIT) {
		mutex_unlock(&core->lock);
		return; 
	}
	pldev = subdev->pldev;
	state = subdev->state;
	subdev->pldev = NULL;
	subdev->ops = NULL;
	subdev->state = XOCL_SUBDEV_STATE_UNINIT;
	ida_simple_remove(&subdev_inst_ida, subdev->inst);

	mutex_unlock(&core->lock);

	if (pldev) {
		if (state == XOCL_SUBDEV_STATE_INIT) {
			device_release_driver(&pldev->dev);
			platform_device_unregister(pldev);
		}
		platform_device_put(pldev);
	}
}

int xocl_subdev_create(xdev_handle_t xdev_hdl,
	struct xocl_subdev_info *sdev_info)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct xocl_subdev *subdev;
	void *priv_data;
	resource_size_t iostart;
	struct resource *res = NULL;
	int i, retval;

	subdev = xocl_subdev_reserve(xdev_hdl, sdev_info);
	if (!subdev) {
		if (!sdev_info->multi_inst)
			retval = -EEXIST;
		else
			retval = -ENOENT;
		goto error;
	}

	memcpy(&subdev->info, sdev_info, sizeof(subdev->info));

	if (sdev_info->num_res > 0) {
		if (sdev_info->num_res > XOCL_SUBDEV_MAX_RES) {
			xocl_xdev_err(xdev_hdl, "Too many resources %d\n",
					sdev_info->num_res);
			retval = -EINVAL;
			goto error;
		}
		res = subdev->res;
		memcpy(res, sdev_info->res, sizeof (*res) * sdev_info->num_res);
	}

	if ((sdev_info->level == XOCL_SUBDEV_LEVEL_BLD || 
		sdev_info->level == XOCL_SUBDEV_LEVEL_PRP) &&
	   	sdev_info->pf != XOCL_PCI_FUNC(xdev_hdl)) {
		xocl_xdev_info(xdev_hdl, "Cache subdev %s id %d pf %d",
			sdev_info->name, subdev->inst, sdev_info->pf);
		return 0;
	}	

	xocl_xdev_info(xdev_hdl, "creating subdev %s id %d",
			sdev_info->name, subdev->inst);
	subdev->pldev = platform_device_alloc(sdev_info->name, subdev->inst);
	if (!subdev->pldev) {
		xocl_xdev_err(xdev_hdl, "failed to alloc device %s",
			sdev_info->name);
		retval = -ENOMEM;
		goto error;
	}

	if (res) {
		iostart = (sdev_info->level == XOCL_SUBDEV_LEVEL_STATIC) ?
			pci_resource_start(core->pdev, core->bar_idx) :
			pci_resource_start(core->pdev, sdev_info->bar_idx);

		for (i = 0; i < sdev_info->num_res; i++) {
			if (sdev_info->res[i].flags & IORESOURCE_MEM) {
				res[i].start += iostart;
				res[i].end += iostart;
			}
		}

		retval = platform_device_add_resources(subdev->pldev,
			res, sdev_info->num_res);
		if (retval) {
			xocl_xdev_err(xdev_hdl, "failed to add res");
			goto error;
		}

	}

	if (sdev_info->data_len > 0) {
		priv_data = vzalloc(sdev_info->data_len);
		memcpy(priv_data, sdev_info->priv_data,
				sdev_info->data_len);
		retval = platform_device_add_data(subdev->pldev, priv_data,
			sdev_info->data_len);
		vfree(priv_data);
		if (retval) {
			xocl_xdev_err(xdev_hdl, "failed to add data");
			goto error;
		}
	}

	subdev->pldev->dev.parent = &core->pdev->dev;

	retval = platform_device_add(subdev->pldev);
	if (retval) {
		xocl_xdev_err(xdev_hdl, "failed to add device");
		goto error;
	}

	xocl_xdev_info(xdev_hdl, "Created subdev %s id %d",
			sdev_info->name, subdev->inst);

	/*
	 * force probe to avoid dependence issue. if probing
	 * failed, it could be the driver is not registered.
	 */
	retval = device_attach(&subdev->pldev->dev);
	if (retval != 1) {
		/* return error without release. relies on caller to decide
		   if this is an error or not */
		xocl_xdev_info(xdev_hdl, "failed to probe subdev %s, ret %d",
			sdev_info->name, retval);
		return -EAGAIN;
	}
	xocl_xdev_info(xdev_hdl, "subdev %s id %d is active",
			sdev_info->name, subdev->inst);

	return 0;

error:
	if (subdev)
		xocl_subdev_destroy(xdev_hdl, subdev);

	return retval;
}

int xocl_subdev_create_by_name(xdev_handle_t xdev_hdl, char *name)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i, n;

	for (i = 0; i < core->priv.subdev_num; i++) {
		n = strlen(name);
		if (name[n - 1] == '\n')
			n--;
		if (!strncmp(core->priv.subdev_info[i].name, name, n))
			break;
	}
	if (i == core->priv.subdev_num)
		return -ENODEV;

	return xocl_subdev_create(xdev_hdl, 
			&core->priv.subdev_info[i]);
}

int xocl_subdev_destroy_by_name(xdev_handle_t xdev_hdl, char *name)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i, n;

	for (i = 0; i < core->priv.subdev_num; i++) {
		n = strlen(name);
		if (name[n - 1] == '\n')
			n--;
		if (!strncmp(core->priv.subdev_info[i].name, name, n))
			break;
	}
	if (i == core->priv.subdev_num)
		return -ENODEV;

	xocl_subdev_destroy_by_id(xdev_hdl, core->priv.subdev_info[i].id);

	return 0;
}

int xocl_subdev_create_by_id(xdev_handle_t xdev_hdl, int id)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i;

	for (i = 0; i < core->priv.subdev_num; i++)
		if (core->priv.subdev_info[i].id == id)
			break;
	if (i == core->priv.subdev_num)
		return -ENOENT;

	return xocl_subdev_create(xdev_hdl, 
			&core->priv.subdev_info[i]);
}

int xocl_subdev_create_all(xdev_handle_t xdev_hdl,
	struct xocl_subdev_info *sdev_info, u32 subdev_num)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct FeatureRomHeader rom;
	u32	id;
	int	i, ret = 0;

	/* lookup update table */
	ret = xocl_subdev_create(xdev_hdl,
		&(struct xocl_subdev_info)XOCL_DEVINFO_FEATURE_ROM);
	if (ret && ret != -EEXIST)
		goto failed;

	for (i = 0; i < ARRAY_SIZE(dsa_vbnv_map); i++) {
		xocl_get_raw_header(core, &rom);
		if ((core->pdev->vendor == dsa_vbnv_map[i].vendor ||
			dsa_vbnv_map[i].vendor == (u16)PCI_ANY_ID) &&
			(core->pdev->device == dsa_vbnv_map[i].device ||
			dsa_vbnv_map[i].device == (u16)PCI_ANY_ID) &&
			(core->pdev->subsystem_device ==
			dsa_vbnv_map[i].subdevice ||
			dsa_vbnv_map[i].subdevice == (u16)PCI_ANY_ID) &&
			!strncmp(rom.VBNVName, dsa_vbnv_map[i].vbnv,
			sizeof(rom.VBNVName))) {
			sdev_info = dsa_vbnv_map[i].priv_data->subdev_info;
			subdev_num = dsa_vbnv_map[i].priv_data->subdev_num;
			xocl_fill_dsa_priv(xdev_hdl, dsa_vbnv_map[i].priv_data);
			break;
		}
	}

	/* create subdevices */
	for (i = 0; i < subdev_num; i++) {
		id = sdev_info[i].id;

		ret = xocl_subdev_create(xdev_hdl, &sdev_info[i]);
		if (ret && ret != -EEXIST)
			goto failed;
	}

	return 0;

failed:
	xocl_subdev_destroy_all(xdev_hdl);
	return ret;
}

void xocl_subdev_destroy_by_id(xdev_handle_t xdev_hdl, uint32_t subdev_id)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i;

	if (subdev_id==INVALID_SUBDEVICE)
		return;

	for (i = 0; i < XOCL_SUBDEV_MAX_INST; i++)
		xocl_subdev_destroy(xdev_hdl, &core->subdevs[subdev_id][i]);
}

void xocl_subdev_destroy_all(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int	i;

	for (i = ARRAY_SIZE(core->subdevs) - 1; i >= 0; i--)
		xocl_subdev_destroy_by_id(xdev_hdl, i);
}

void xocl_subdev_destroy_by_level(xdev_handle_t xdev_hdl, int level)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int 		i, j;

	for (i = ARRAY_SIZE(core->subdevs) - 1; i >= 0; i--)
		for (j = 0; j < XOCL_SUBDEV_MAX_INST; j++)
			if (core->subdevs[i][j].info.level == level)
				xocl_subdev_destroy(xdev_hdl,
					&core->subdevs[i][j]);
}

int xocl_subdev_offline(xdev_handle_t xdev_hdl, struct xocl_subdev *subdev)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct xocl_subdev_funcs *subdev_funcs;
	struct platform_device *pldev = NULL;
	int ret = 0;

	mutex_lock(&core->lock);
	if (subdev->state != XOCL_SUBDEV_STATE_INIT) {
		if (subdev->state == XOCL_SUBDEV_STATE_OFFLINE) {
			xocl_xdev_err(xdev_hdl, "%s, already offline",
				subdev->info.name);
		} else {
			xocl_xdev_err(xdev_hdl, "%s, Invalid state %d",
				subdev->info.name, subdev->state);
			ret = -EINVAL;
		}
		mutex_unlock(&core->lock);
		goto done;
	}
	subdev->state = XOCL_SUBDEV_STATE_OFFLINE;

	subdev_funcs = subdev->ops;
	if(subdev_funcs && subdev_funcs->offline) {
		ret = subdev_funcs->offline(subdev->pldev);
		goto done;
	} else {
		pldev = subdev->pldev;
		subdev->ops = NULL;
	}

done:
	mutex_unlock(&core->lock);

	if (pldev) {
		device_release_driver(&pldev->dev);
		platform_device_del(pldev);
	}

	return ret;
}

int xocl_subdev_online(xdev_handle_t xdev_hdl, struct xocl_subdev *subdev)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct xocl_subdev_funcs *subdev_funcs;
	struct platform_device *pldev = NULL;
	int ret = 0;

	mutex_lock(&core->lock);
	if (subdev->state != XOCL_SUBDEV_STATE_OFFLINE) {
		if (subdev->state == XOCL_SUBDEV_STATE_INIT) {
			xocl_xdev_err(xdev_hdl, "%s, already online",
				subdev->info.name);
		} else {
			xocl_xdev_err(xdev_hdl, "%s, Invalid state %d",
				subdev->info.name, subdev->state);
			ret = -EINVAL;
		}
		mutex_unlock(&core->lock);
		goto failed;
	}

	subdev_funcs = subdev->ops;
	if(subdev_funcs && subdev_funcs->online)
		ret = subdev_funcs->online(subdev->pldev);
	else
		pldev = subdev->pldev;

	subdev->state = XOCL_SUBDEV_STATE_INIT;

failed:
	mutex_unlock(&core->lock);

	if (pldev) {
		ret = platform_device_add(subdev->pldev);
		if (ret) {
			xocl_subdev_offline(xdev_hdl, subdev);
			xocl_xdev_err(xdev_hdl, "add device failed %d", ret);
		} else {
			ret = device_attach(&subdev->pldev->dev);
			if (ret != 1) {
				xocl_xdev_info(xdev_hdl,
					"driver is not attached at this time");
				ret = -EAGAIN;
			} else
				ret = 0;
		}
	}

	return ret;
}

int xocl_subdev_offline_by_id(xdev_handle_t xdev_hdl, uint32_t subdev_id)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i, ret = 0;

	if (subdev_id==INVALID_SUBDEVICE)
		return -EINVAL;

	for (i = 0; i < XOCL_SUBDEV_MAX_INST; i++) {
		if (!core->subdevs[subdev_id][i].pldev)
			continue;
		ret = xocl_subdev_offline(xdev_hdl,
				&core->subdevs[subdev_id][i]);
		if (ret)
			break;
	}

	return ret;
}

int xocl_subdev_online_by_id(xdev_handle_t xdev_hdl, uint32_t subdev_id)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int i, ret = 0;

	if (subdev_id==INVALID_SUBDEVICE)
		return -EINVAL;

	for (i = 0; i < XOCL_SUBDEV_MAX_INST; i++) {
		if (!core->subdevs[subdev_id][i].pldev)
			continue;
		ret = xocl_subdev_online(xdev_hdl,
				&core->subdevs[subdev_id][i]);
		if (ret && ret != -EAGAIN)
			break;
	}

	return (ret && ret != -EAGAIN) ? ret : 0;
}

int xocl_subdev_offline_all(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int ret = 0, i;

	/* If subdev driver registered offline/online callback,
	 * call offline. Otherwise, fallback to detach the subdevice
	 * Currenly, assume the offline will remove the subdev
	 * dependency as well.
	 */
	for (i = ARRAY_SIZE(core->subdevs) - 1; i >= 0; i--) {
		ret = xocl_subdev_offline_by_id(xdev_hdl, i);
		if (ret)
			break;
	}

	return ret;
}

int xocl_subdev_online_all(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	int ret = 0, i;

	for (i = 0; i < ARRAY_SIZE(core->subdevs); i++) {
		ret = xocl_subdev_online_by_id(xdev_hdl, i);
		if (ret)
			break;
	}

	return ret;
}

void xocl_subdev_register(struct platform_device *pldev, u32 id,
	void *cb_funcs)
{
	struct xocl_dev_core		*core;
	int i, j;

	BUG_ON(id >= XOCL_SUBDEV_NUM);
	core = xocl_get_xdev(pldev);
	BUG_ON(!core);

	mutex_lock(&core->lock);
	for (j = 0; j < XOCL_SUBDEV_NUM; j++)
		for (i = 0; i < XOCL_SUBDEV_MAX_INST; i++)
			if (core->subdevs[j][i].pldev == pldev) {
				core->subdevs[j][i].ops = cb_funcs;
				mutex_unlock(&core->lock);
				return;
			}
	mutex_unlock(&core->lock);
}

xdev_handle_t xocl_get_xdev(struct platform_device *pdev)
{
	struct device *dev;

	dev = pdev->dev.parent;

	return dev ? pci_get_drvdata(to_pci_dev(dev)) : NULL;
}

void xocl_fill_dsa_priv(xdev_handle_t xdev_hdl, struct xocl_board_private *in)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;
	struct pci_dev *pdev = core->pdev;
	unsigned int i;

	memset(&core->priv, 0, sizeof(core->priv));
	/*
 	 * follow xilinx device id, subsystem id codeing rules to set dsa
	 * private data. And they can be overwrited in subdev header file
	 */
	if ((pdev->device >> 5) & 0x1) {
		core->priv.xpr = true;
	}
	core->priv.dsa_ver = pdev->subsystem_device & 0xff;

	/* data defined in subdev header */
	core->priv.subdev_info = in->subdev_info;
	core->priv.subdev_num = in->subdev_num;
	core->priv.flags = in->flags;
	core->priv.flash_type = in->flash_type;
	core->priv.board_name = in->board_name;
	core->priv.mpsoc = in->mpsoc;
	if (in->flags & XOCL_DSAFLAG_SET_DSA_VER)
		core->priv.dsa_ver = in->dsa_ver;
	if (in->flags & XOCL_DSAFLAG_SET_XPR)
		core->priv.xpr = in->xpr;

	for (i = 0; i < in->subdev_num; i++) {
		if (in->subdev_info[i].id == XOCL_SUBDEV_FEATURE_ROM) {
			core->feature_rom_offset =
				in->subdev_info[i].res[0].start;
			break;
		}
	}
}

int xocl_xrt_version_check(xdev_handle_t xdev_hdl,
	struct axlf *bin_obj, bool major_only)
{
	u32 major, minor, patch;
	/* check runtime version:
	 *    1. if it is 0.0.xxxx, this implies old xclbin,
	 *       we pass the check anyway.
	 *    2. compare major and minor, returns error if it does not match.
	 */
	sscanf(xrt_build_version, "%d.%d.%d", &major, &minor, &patch);
	if (major != bin_obj->m_header.m_versionMajor &&
		bin_obj->m_header.m_versionMajor != 0)
		goto err;

	if (major_only)
		return 0;

	if ((major != bin_obj->m_header.m_versionMajor ||
		minor != bin_obj->m_header.m_versionMinor) &&
		!(bin_obj->m_header.m_versionMajor == 0 &&
		bin_obj->m_header.m_versionMinor == 0))
		goto err;

	return 0;

err:
	xocl_err(&XDEV(xdev_hdl)->pdev->dev,
		"Mismatch xrt version, xrt %s, xclbin "
		"%d.%d.%d", xrt_build_version,
		bin_obj->m_header.m_versionMajor,
		bin_obj->m_header.m_versionMinor,
		bin_obj->m_header.m_versionPatch);

	return -EINVAL;
}

int xocl_alloc_dev_minor(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;

	core->dev_minor = ida_simple_get(&xocl_dev_minor_ida,
		0, 0, GFP_KERNEL);

	if (core->dev_minor < 0) {
		xocl_err(&core->pdev->dev, "Failed to alloc dev minor");
		core->dev_minor = XOCL_INVALID_MINOR;
		return -ENOENT;
	}

	return 0;
}

void xocl_free_dev_minor(xdev_handle_t xdev_hdl)
{
	struct xocl_dev_core *core = (struct xocl_dev_core *)xdev_hdl;

	if (core->dev_minor != XOCL_INVALID_MINOR) {
		ida_simple_remove(&xocl_dev_minor_ida, core->dev_minor);
		core->dev_minor = XOCL_INVALID_MINOR;
	}
}
