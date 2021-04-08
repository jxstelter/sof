// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Marcin Rajwa <marcin.rajwa@linux.intel.com>

/*
 * \file cadence.c
 * \brief Cadence Codec API
 * \author Marcin Rajwa <marcin.rajwa@linux.intel.com>
 *
 */

#include <sof/audio/codec_adapter/codec/generic.h>
#include <sof/audio/codec_adapter/codec/cadence.h>

/*****************************************************************************/
/* Cadence API functions array						     */
/*****************************************************************************/
static struct cadence_api cadence_api_table[] = {
#ifdef CONFIG_CADENCE_CODEC_WRAPPER
	{
		.id = 0x01,
		.api = cadence_api_function
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_AAC_DEC
	{
		.id = 0x02,
		.api = xa_aac_dec,
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_BSAC_DEC
	{
		.id = 0x03,
		.api = xa_bsac_dec,
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_DAB_DEC
	{
		.id = 0x04,
		.api = xa_dabplus_dec,
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_DRM_DEC
	{
		.id = 0x05,
		.api = xa_drm_dec,
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_MP3_DEC
	{
		.id = 0x06,
		.api = xa_mp3_dec,
	},
#endif
#ifdef CONFIG_CADENCE_CODEC_SBC_DEC
	{
		.id = 0x07,
		.api = xa_sbc_dec,
	},
#endif
};

int cadence_codec_init(struct comp_dev *dev)
{
	int ret;
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = NULL;
	uint32_t obj_size;
	uint32_t no_of_api = ARRAY_SIZE(cadence_api_table);
	uint32_t api_id = CODEC_GET_API_ID(codec->id);
	uint32_t i;

	comp_dbg(dev, "cadence_codec_init() start");

	cd = codec_allocate_memory(dev, sizeof(struct cadence_codec_data), 0);
	if (!cd) {
		comp_err(dev, "cadence_codec_init(): failed to allocate memory for cadence codec data");
		return -ENOMEM;
	}

	codec->private = cd;
	cd->self = NULL;
	cd->mem_tabs = NULL;
	cd->api = NULL;

	/* Find and assign API function */
	for (i = 0; i < no_of_api; i++) {
		if (cadence_api_table[i].id == api_id) {
			cd->api = cadence_api_table[i].api;
			break;
		}
	}
	/* Verify API assignment */
	if (!cd->api) {
		comp_err(dev, "cadence_codec_init(): could not find API function for id %x",
			 api_id);
		ret = -EINVAL;
		goto out;
	}

	/* Obtain codec name */
	API_CALL(cd, XA_API_CMD_GET_LIB_ID_STRINGS,
		 XA_CMD_TYPE_LIB_NAME, cd->name, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_init() error %x: failed to get lib name",
			 ret);
		codec_free_memory(dev, cd);
		goto out;
	}
	/* Get codec object size */
	API_CALL(cd, XA_API_CMD_GET_API_SIZE, 0, &obj_size, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_init() error %x: failed to get lib object size",
			 ret);
		codec_free_memory(dev, cd);
		goto out;
	}
	/* Allocate space for codec object */
	cd->self = codec_allocate_memory(dev, obj_size, 0);
	if (!cd->self) {
		comp_err(dev, "cadence_codec_init(): failed to allocate space for lib object");
		codec_free_memory(dev, cd);
		goto out;
	} else {
		comp_dbg(dev, "cadence_codec_init(): allocated %d bytes for lib object",
			 obj_size);
	}

	comp_dbg(dev, "cadence_codec_init() done");
out:
	return ret;
}

static int update_stream_params(struct comp_dev *dev)
{
	int ret = 0;
	struct comp_data *ca_data = comp_get_drvdata(dev);
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = codec->private;
	struct ca_config *ca_config = &ca_data->ca_config;
	struct sof_ipc_stream_params *stream = &ca_data->stream_params;
	/* copy stream params localy to ensure container size is correct */
	uint32_t sample_width = (stream->frame_fmt == SOF_IPC_FRAME_S16_LE) ? 0x10 : 0x20;
	uint32_t sample_rate = stream->rate;
	uint32_t channels = stream->channels;

	comp_err(dev, "update_stream_params() start");

	/* update stream parameters */
	/* sample rate */
	API_CALL(cd, XA_API_CMD_SET_CONFIG_PARAM, ca_config->sample_rate_id,
		 &sample_rate, ret);
	if (LIB_IS_FATAL_ERROR(ret)) {
		comp_err(dev, "update_stream_params(): failed to set sample rate.");
		goto err;
	}
	/* sample width */
	API_CALL(cd, XA_API_CMD_SET_CONFIG_PARAM, ca_config->sample_width_id,
		 &sample_width, ret);
	if (LIB_IS_FATAL_ERROR(ret)) {
		comp_err(dev, "update_stream_params(): failed to set sample width.");
		goto err;
	}
	/* number of channels */
	API_CALL(cd, XA_API_CMD_SET_CONFIG_PARAM, ca_config->channels_id,
		 &channels, ret);
	if (LIB_IS_FATAL_ERROR(ret)) {
		comp_err(dev, "update_stream_params(): failed to set channel count.");
		goto err;
	}
err:
	comp_err(dev, "update_stream_params() done, returned %d", ret);
	return ret;
}

static int apply_config(struct comp_dev *dev, enum codec_cfg_type type)
{
	int ret = 0;
	int size;
	struct codec_config *cfg;
	void *data;
	struct codec_param *param;
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = codec->private;

	comp_dbg(dev, "apply_config() start");

	cfg = (type == CODEC_CFG_SETUP) ? &codec->s_cfg :
					  &codec->r_cfg;
	data = cfg->data;
	size = cfg->size;

	if (!cfg->avail || !size) {
		comp_err(dev, "apply_config() error: no config available, requested conf. type %d",
			 type);
		ret = -EIO;
		goto ret;
	}

	/* Read parameters stored in `data` - it may keep plenty of
	 * parameters. The `size` variable is equal to param->size * count,
	 * where count is number of parameters stored in `data`.
	 */
	while (size > 0) {
		param = data;
		comp_dbg(dev, "apply_config() applying param %d value %d",
			 param->id, param->data[0]);
		/* Set read parameter */
		API_CALL(cd, XA_API_CMD_SET_CONFIG_PARAM, param->id,
			 param->data, ret);
		if (ret != LIB_NO_ERROR) {
			comp_err(dev, "apply_config() error %x: failed to apply parameter %d value %d",
				 ret, param->id, *(int32_t *)param->data);
			if (LIB_IS_FATAL_ERROR(ret))
				goto ret;
		}
		/* Obtain next parameter, it starts right after the preceding one */
		data = (char *)data + param->size;
		size -= param->size;
	}

	comp_dbg(dev, "apply_config() done");
	ret  = 0;
ret:
	return ret;
}

static int init_memory_tables(struct comp_dev *dev)
{
	int ret, no_mem_tables, i, mem_type, mem_size, mem_alignment;
	void *ptr, *scratch, *persistent;
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = codec->private;

	scratch = NULL;
	persistent = NULL;

	/* Calculate the size of all memory blocks required */
	API_CALL(cd, XA_API_CMD_INIT, XA_CMD_TYPE_INIT_API_POST_CONFIG_PARAMS,
		 NULL, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "init_memory_tables() error %x: failed to calculate memory blocks size",
			 ret);
		goto err;
	}

	/* Get number of memory tables */
	API_CALL(cd, XA_API_CMD_GET_N_MEMTABS, 0, &no_mem_tables, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "init_memory_tables() error %x: failed to get number of memory tables",
			 ret);
		goto err;
	}

	/* Initialize each memory table */
	for (i = 0; i < no_mem_tables; i++) {
		/* Get type of memory - it specifies how the memory will be used */
		API_CALL(cd, XA_API_CMD_GET_MEM_INFO_TYPE, i, &mem_type, ret);
		if (ret != LIB_NO_ERROR) {
			comp_err(dev, "init_memory_tables() error %x: failed to get mem. type info of id %d out of %d",
				 ret, i, no_mem_tables);
			goto err;
		}
		/* Get size of memory needed for memory allocation for this
		 * particular memory type.
		 */
		API_CALL(cd, XA_API_CMD_GET_MEM_INFO_SIZE, i, &mem_size, ret);
		if (ret != LIB_NO_ERROR) {
			comp_err(dev, "init_memory_tables() error %x: failed to get mem. size for mem. type %d",
				 ret, mem_type);
			goto err;
		}
		/* Get alignment constrains */
		API_CALL(cd, XA_API_CMD_GET_MEM_INFO_ALIGNMENT, i, &mem_alignment, ret);
		if (ret != LIB_NO_ERROR) {
			comp_err(dev, "init_memory_tables() error %x: failed to get mem. alignment of mem. type %d",
				 ret, mem_type);
			goto err;
		}
		/* Allocate memory for this type, taking alignment into account */
		ptr = codec_allocate_memory(dev, mem_size, mem_alignment);
		if (!ptr) {
			comp_err(dev, "init_memory_tables() error %x: failed to allocate memory for %d",
				 ret, mem_type);
			ret = -EINVAL;
			goto err;
		}
		/* Finally, provide this memory for codec */
		API_CALL(cd, XA_API_CMD_SET_MEM_PTR, i, ptr, ret);
		if (ret != LIB_NO_ERROR) {
			comp_err(dev, "init_memory_tables() error %x: failed to set memory pointer for %d",
				 ret, mem_type);
			goto err;
		}

		switch ((unsigned int)mem_type) {
		case XA_MEMTYPE_SCRATCH:
			scratch = ptr;
			break;
		case XA_MEMTYPE_PERSIST:
			persistent = ptr;
			break;
		case XA_MEMTYPE_INPUT:
			codec->cpd.in_buff = ptr;
			codec->cpd.in_buff_size = mem_size;
			break;
		case XA_MEMTYPE_OUTPUT:
			codec->cpd.out_buff = ptr;
			codec->cpd.out_buff_size = mem_size;
			break;
		default:
			comp_err(dev, "init_memory_tables() error %x: unrecognized memory type!",
				 mem_type);
			ret = -EINVAL;
			goto err;
		}

		comp_dbg(dev, "init_memory_tables: allocated memory of %d bytes and alignment %d for mem. type %d",
			 mem_size, mem_alignment, mem_type);
	}

	return 0;
err:
	if (scratch)
		codec_free_memory(dev, scratch);
	if (persistent)
		codec_free_memory(dev, persistent);
	if (codec->cpd.in_buff)
		codec_free_memory(dev, codec->cpd.in_buff);
	if (codec->cpd.out_buff)
		codec_free_memory(dev, codec->cpd.out_buff);
	return ret;
}

int cadence_codec_prepare(struct comp_dev *dev)
{
	int ret = 0, mem_tabs_size, lib_init_status;
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = codec->private;

	comp_dbg(dev, "cadence_codec_prepare() start");

	if (codec->state == CODEC_PREPARED)
		goto done;

	API_CALL(cd, XA_API_CMD_INIT, XA_CMD_TYPE_INIT_API_PRE_CONFIG_PARAMS,
		 NULL, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare(): error %x: failed to set default config",
			 ret);
		goto err;
	}

	if (!codec->s_cfg.avail && !codec->s_cfg.size) {
		comp_err(dev, "cadence_codec_prepare() no setup configuration available!");
		ret = -EIO;
		goto err;
	} else if (!codec->s_cfg.avail) {
		comp_warn(dev, "cadence_codec_prepare(): no new setup configuration available, using the old one");
		codec->s_cfg.avail = true;
	}
	ret = apply_config(dev, CODEC_CFG_SETUP);
	if (ret) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to applay setup config",
			 ret);
		goto err;
	}
	/* Do not reset nor free codec setup config "size" so we can use it
	 * later on in case there is no new one upon reset.
	 */
	codec->s_cfg.avail = false;

	/* Update codec config with stream parameters */
	ret = update_stream_params(dev);
	if (ret) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to update stream params",
			 ret);
		goto err;
	}
	/* Allocate memory for the codec */
	API_CALL(cd, XA_API_CMD_GET_MEMTABS_SIZE, 0, &mem_tabs_size, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to get memtabs size",
			 ret);
		goto err;
	}

	cd->mem_tabs = codec_allocate_memory(dev, mem_tabs_size, 4);
	if (!cd->mem_tabs) {
		comp_err(dev, "cadence_codec_prepare() error: failed to allocate space for memtabs");
		ret = -ENOMEM;
		goto err;
	} else {
		comp_dbg(dev, "cadence_codec_prepare(): allocated %d bytes for memtabs",
			 mem_tabs_size);
	}

	API_CALL(cd, XA_API_CMD_SET_MEMTABS_PTR, 0, cd->mem_tabs, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to set memtabs",
			 ret);
		goto free;
	}

	ret = init_memory_tables(dev);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to init memory tables",
			 ret);
		goto free;
	}

	API_CALL(cd, XA_API_CMD_INIT, XA_CMD_TYPE_INIT_PROCESS, NULL, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to initialize codec",
			 ret);
		goto free;
	}

	API_CALL(cd, XA_API_CMD_INIT, XA_CMD_TYPE_INIT_DONE_QUERY,
		 &lib_init_status, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_prepare() error %x: failed to get lib init status",
			 ret);
		goto free;
	} else if (!lib_init_status) {
		comp_err(dev, "cadence_codec_prepare() error: lib has not been initiated properly");
		ret = -EINVAL;
		goto free;
	} else {
		comp_dbg(dev, "cadence_codec_prepare(): lib has been initialized properly");
	}

	comp_dbg(dev, "cadence_codec_prepare() done");
	return 0;
free:
	codec_free_memory(dev, cd->mem_tabs);
err:
done:
	return ret;
}

int cadence_codec_process(struct comp_dev *dev)
{
	int ret;
	struct codec_data *codec = comp_get_codec(dev);
	struct cadence_codec_data *cd = codec->private;

	comp_dbg(dev, "cadence_codec_process() start");

	API_CALL(cd, XA_API_CMD_SET_INPUT_BYTES, 0, &codec->cpd.avail, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_process() error %x: failed to set size of input data",
			 ret);
		goto err;
	}

	API_CALL(cd, XA_API_CMD_EXECUTE, XA_CMD_TYPE_DO_EXECUTE, NULL, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_process() error %x: processing failed",
			 ret);
		goto err;
	}

	API_CALL(cd, XA_API_CMD_GET_OUTPUT_BYTES, 0, &codec->cpd.produced, ret);
	if (ret != LIB_NO_ERROR) {
		comp_err(dev, "cadence_codec_process() error %x: could not get produced bytes",
			 ret);
		goto err;
	}

	comp_dbg(dev, "cadence_codec_process() done");

	return 0;
err:
	return ret;
}

int cadence_codec_apply_config(struct comp_dev *dev)
{
	return apply_config(dev, CODEC_CFG_RUNTIME);
}

int cadence_codec_reset(struct comp_dev *dev)
{
	int ret;
	/* Current CADENCE API doesn't support reset of codec's
	 * runtime parameters therefore we need to free all the resources
	 * and start over.
	 */
	codec_free_all_memory(dev);
	ret = cadence_codec_init(dev);
	if (ret) {
		comp_err(dev, "cadence_codec_reset() error %x: could not reinitialize codec after reset",
			 ret);
	}

	return ret;
}

int cadence_codec_free(struct comp_dev *dev)
{
	/* Nothing to do */
	return 0;
}
