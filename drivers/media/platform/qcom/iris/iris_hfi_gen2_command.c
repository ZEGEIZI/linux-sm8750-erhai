// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>

#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_packet.h"

#define UNSPECIFIED_COLOR_FORMAT 5
#define NUM_SYS_INIT_PACKETS 8

#define SYS_INIT_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	NUM_SYS_INIT_PACKETS * (sizeof(struct iris_hfi_packet) + sizeof(u32)))

#define SYS_IFPC_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet) + sizeof(u32))

#define SYS_NO_PAYLOAD_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet))

static int iris_hfi_gen2_sys_init(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_INIT_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_init(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_image_version(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_image_version(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_interframe_powercollapse(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_IFPC_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_interframe_powercollapse(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_pc_prep(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_pc_prep(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static u32 iris_hfi_gen2_get_port(struct iris_inst *inst, u32 plane)
{
	if (inst->domain == DECODER) {
		switch (plane) {
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			return HFI_PORT_BITSTREAM;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			return HFI_PORT_RAW;
		default:
			return HFI_PORT_NONE;
		}
	} else {
		switch (plane) {
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			return HFI_PORT_RAW;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			return HFI_PORT_BITSTREAM;
		default:
			return HFI_PORT_NONE;
		}
	}
}

static u32 iris_hfi_gen2_get_port_from_buf_type(struct iris_inst *inst,
						enum iris_buffer_type buffer_type)
{
	if (inst->domain == DECODER) {
		switch (buffer_type) {
		case BUF_INPUT:
		case BUF_BIN:
		case BUF_COMV:
		case BUF_NON_COMV:
		case BUF_LINE:
		case BUF_PARTIAL:
			return HFI_PORT_BITSTREAM;
		case BUF_OUTPUT:
		case BUF_DPB:
			return HFI_PORT_RAW;
		case BUF_PERSIST:
		default:
			return HFI_PORT_NONE;
		}
	} else {
		switch (buffer_type) {
		case BUF_INPUT:
		case BUF_VPSS:
			return HFI_PORT_RAW;
		case BUF_OUTPUT:
		case BUF_BIN:
		case BUF_COMV:
		case BUF_NON_COMV:
		case BUF_LINE:
		case BUF_SCRATCH_2:
			return HFI_PORT_BITSTREAM;
		case BUF_ARP:
		default:
			return HFI_PORT_NONE;
		}
	}
}

static int iris_hfi_gen2_session_set_property(struct iris_inst *inst, u32 packet_type, u32 flag,
					      u32 plane, u32 payload_type, void *payload,
					      u32 payload_size)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);

	iris_hfi_gen2_packet_session_property(inst,
					      packet_type,
					      flag,
					      plane,
					      payload_type,
					      payload,
					      payload_size);

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_set_raw_resolution(struct iris_inst *inst, u32 plane)
{
	u32 resolution = inst->enc_raw_width << 16 | inst->enc_raw_height;
	u32 port = iris_hfi_gen2_get_port(inst, plane);

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_RAW_RESOLUTION,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_32_PACKED,
						  &resolution,
						  sizeof(u32));
}

static inline u32 iris_hfi_get_aligned_resolution(struct iris_inst *inst, u32 width, u32 height)
{
	u32 codec_align = inst->codec == V4L2_PIX_FMT_HEVC ? 32 : 16;

	return (ALIGN(width, codec_align) << 16 | ALIGN(height, codec_align));
}

static int iris_hfi_gen2_set_bitstream_resolution(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	enum hfi_packet_payload_info payload_type;
	u32 width, height;
	u32 resolution;

	if (inst->domain == DECODER) {
		width = inst->fmt_src->fmt.pix_mp.width;
		height = inst->fmt_src->fmt.pix_mp.height;
		resolution = iris_hfi_get_aligned_resolution(inst, width, height);
		inst_hfi_gen2->src_subcr_params.bitstream_resolution = resolution;
		payload_type = HFI_PAYLOAD_U32;
	} else {
		if (is_rotation_90_or_270(inst)) {
			width = inst->enc_scale_height;
			height = inst->enc_scale_width;
		} else {
			width = inst->enc_scale_width;
			height = inst->enc_scale_height;
		}
		resolution = iris_hfi_get_aligned_resolution(inst, width, height);
		inst_hfi_gen2->dst_subcr_params.bitstream_resolution = resolution;
		payload_type = HFI_PAYLOAD_32_PACKED;
	}

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_BITSTREAM_RESOLUTION,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  payload_type,
						  &resolution,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_crop_offsets(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 bottom_offset, right_offset;
	u32 left_offset, top_offset;
	u32 payload[2], codec_align;

	if (inst->domain == DECODER) {
		if (V4L2_TYPE_IS_OUTPUT(plane)) {
			bottom_offset = (inst->fmt_src->fmt.pix_mp.height - inst->crop.height);
			right_offset = (inst->fmt_src->fmt.pix_mp.width - inst->crop.width);
			left_offset = inst->crop.left;
			top_offset = inst->crop.top;
		} else {
			bottom_offset = (inst->fmt_dst->fmt.pix_mp.height - inst->compose.height);
			right_offset = (inst->fmt_dst->fmt.pix_mp.width - inst->compose.width);
			left_offset = inst->compose.left;
			top_offset = inst->compose.top;
		}
	} else {
		codec_align = inst->codec == V4L2_PIX_FMT_HEVC ? 32 : 16;
		if (V4L2_TYPE_IS_OUTPUT(plane)) {
			bottom_offset = (inst->enc_raw_height - inst->crop.height);
			right_offset = (inst->enc_raw_width - inst->crop.width);
			left_offset = inst->crop.left;
			top_offset = inst->crop.top;
		} else {
			if (is_rotation_90_or_270(inst)) {
				bottom_offset = (ALIGN(inst->enc_scale_width, codec_align) -
						inst->enc_scale_width);
				right_offset = (ALIGN(inst->enc_scale_height, codec_align) -
					       inst->enc_scale_height);
			} else {
				bottom_offset = (ALIGN(inst->enc_scale_height, codec_align) -
						inst->enc_scale_height);
				right_offset = (ALIGN(inst->enc_scale_width, codec_align) -
					       inst->enc_scale_width);
			}
			left_offset = 0;
			top_offset = 0;
		}
	}

	payload[0] = FIELD_PREP(GENMASK(31, 16), left_offset) | top_offset;
	payload[1] = FIELD_PREP(GENMASK(31, 16), right_offset) | bottom_offset;
	inst_hfi_gen2->src_subcr_params.crop_offsets[0] = payload[0];
	inst_hfi_gen2->src_subcr_params.crop_offsets[1] = payload[1];

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_CROP_OFFSETS,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_64_PACKED,
						  &payload,
						  sizeof(u64));
}

static int iris_hfi_gen2_set_bit_depth(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 bitdepth = BIT_DEPTH_8;

	inst_hfi_gen2->src_subcr_params.bit_depth = bitdepth;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &bitdepth,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_coded_frames(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 coded_frames = 0;

	if (inst->fw_caps[CODED_FRAMES].value == CODED_FRAMES_PROGRESSIVE)
		coded_frames = HFI_BITMASK_FRAME_MBS_ONLY_FLAG;
	inst_hfi_gen2->src_subcr_params.coded_frames = coded_frames;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_CODED_FRAMES,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &coded_frames,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_min_output_count(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 min_output = inst->buffers[BUF_OUTPUT].min_count;
	u32 port = iris_hfi_gen2_get_port(inst, plane);

	inst_hfi_gen2->src_subcr_params.fw_min_count = min_output;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &min_output,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_picture_order_count(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 poc = 0;

	inst_hfi_gen2->src_subcr_params.pic_order_cnt = poc;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_PIC_ORDER_CNT_TYPE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &poc,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_colorspace(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	struct v4l2_pix_format_mplane *pixmp = &inst->fmt_src->fmt.pix_mp;
	u32 video_signal_type_present_flag = 0, color_info;
	u32 matrix_coeff = HFI_MATRIX_COEFF_RESERVED;
	u32 video_format = UNSPECIFIED_COLOR_FORMAT;
	u32 full_range = V4L2_QUANTIZATION_DEFAULT;
	u32 transfer_char = HFI_TRANSFER_RESERVED;
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 colour_description_present_flag = 0;
	u32 primaries = HFI_PRIMARIES_RESERVED;

	if (pixmp->colorspace != V4L2_COLORSPACE_DEFAULT ||
	    pixmp->ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT ||
	    pixmp->xfer_func != V4L2_XFER_FUNC_DEFAULT) {
		colour_description_present_flag = 1;
		video_signal_type_present_flag = 1;
		primaries = iris_hfi_gen2_get_color_primaries(pixmp->colorspace);
		matrix_coeff = iris_hfi_gen2_get_matrix_coefficients(pixmp->ycbcr_enc);
		transfer_char = iris_hfi_gen2_get_transfer_char(pixmp->xfer_func);
	}

	if (pixmp->quantization != V4L2_QUANTIZATION_DEFAULT) {
		video_signal_type_present_flag = 1;
		full_range = pixmp->quantization == V4L2_QUANTIZATION_FULL_RANGE ? 1 : 0;
	}

	color_info = iris_hfi_gen2_get_color_info(matrix_coeff, transfer_char, primaries,
						  colour_description_present_flag, full_range,
						  video_format, video_signal_type_present_flag);

	inst_hfi_gen2->src_subcr_params.color_info = color_info;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_SIGNAL_COLOR_INFO,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_32_PACKED,
						  &color_info,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_profile(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 profile = 0;

	switch (inst->codec) {
	case V4L2_PIX_FMT_HEVC:
		profile = inst->fw_caps[PROFILE_HEVC].value;
		break;
	case V4L2_PIX_FMT_VP9:
		profile = inst->fw_caps[PROFILE_VP9].value;
		break;
	case V4L2_PIX_FMT_H264:
		profile = inst->fw_caps[PROFILE_H264].value;
		break;
	case V4L2_PIX_FMT_AV1:
		profile = inst->fw_caps[PROFILE_AV1].value;
		break;
	}

	inst_hfi_gen2->src_subcr_params.profile = profile;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_PROFILE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &profile,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_level(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 level = 0;

	switch (inst->codec) {
	case V4L2_PIX_FMT_HEVC:
		level = inst->fw_caps[LEVEL_HEVC].value;
		break;
	case V4L2_PIX_FMT_VP9:
		level = inst->fw_caps[LEVEL_VP9].value;
		break;
	case V4L2_PIX_FMT_H264:
		level = inst->fw_caps[LEVEL_H264].value;
		break;
	case V4L2_PIX_FMT_AV1:
		level = inst->fw_caps[LEVEL_AV1].value;
		break;
	}

	inst_hfi_gen2->src_subcr_params.level = level;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LEVEL,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &level,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_opb_enable(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 opb_enable = iris_split_mode_enabled(inst);

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_OPB_ENABLE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &opb_enable,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_colorformat(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 hfi_colorformat, pixelformat;

	if (inst->domain == DECODER) {
		pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
		switch (pixelformat) {
		case V4L2_PIX_FMT_NV12:
			hfi_colorformat = HFI_COLOR_FMT_NV12;
			break;
		case V4L2_PIX_FMT_QC08C:
			hfi_colorformat = HFI_COLOR_FMT_NV12_UBWC;
			break;
		case V4L2_PIX_FMT_P010:
			hfi_colorformat = HFI_COLOR_FMT_P010;
			break;
		case V4L2_PIX_FMT_QC10C:
			hfi_colorformat = HFI_COLOR_FMT_TP10_UBWC;
			break;
		}
	} else {
		pixelformat = inst->fmt_src->fmt.pix_mp.pixelformat;
		hfi_colorformat = pixelformat == V4L2_PIX_FMT_NV12 ?
			HFI_COLOR_FMT_NV12 : HFI_COLOR_FMT_NV12_UBWC;
	}

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_COLOR_FORMAT,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &hfi_colorformat,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_linear_stride_scanline(struct iris_inst *inst, u32 plane)
{
	u32 pixelformat, stride_y, stride_uv, scanline_y, scanline_uv;
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 payload[2];

	if (inst->domain == DECODER) {
		pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
		stride_y = inst->fmt_dst->fmt.pix_mp.width;
		scanline_y = inst->fmt_dst->fmt.pix_mp.height;
	} else {
		pixelformat = inst->fmt_src->fmt.pix_mp.pixelformat;
		stride_y = ALIGN(inst->fmt_src->fmt.pix_mp.width, 128);
		scanline_y = ALIGN(inst->fmt_src->fmt.pix_mp.height, 32);
	}

	stride_uv = stride_y;
	scanline_uv = scanline_y / 2;

	if (pixelformat != V4L2_PIX_FMT_NV12 &&
	    pixelformat != V4L2_PIX_FMT_P010)
		return 0;

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LINEAR_STRIDE_SCANLINE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_64_PACKED,
						  &payload,
						  sizeof(u64));
}

static int iris_hfi_gen2_set_ubwc_stride_scanline(struct iris_inst *inst, u32 plane)
{
	u32 meta_stride_y, meta_scanline_y, meta_stride_uv, meta_scanline_uv;
	u32 stride_y, scanline_y, stride_uv, scanline_uv;
	u32 port = iris_hfi_gen2_get_port(inst, plane);
	u32 pixelformat, width, height;
	u32 payload[4];

	if (inst->domain != DECODER ||
	    inst->fmt_src->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_AV1)
		return 0;

	pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
	width = inst->fmt_dst->fmt.pix_mp.width;
	height = inst->fmt_dst->fmt.pix_mp.height;

	switch (pixelformat) {
	case V4L2_PIX_FMT_QC08C:
		stride_y = ALIGN(width, 128);
		scanline_y = ALIGN(height, 32);
		stride_uv = ALIGN(width, 128);
		scanline_uv = ALIGN((height + 1) >> 1, 32);
		meta_stride_y = ALIGN(DIV_ROUND_UP(width, 32), 64);
		meta_scanline_y = ALIGN(DIV_ROUND_UP(height, 8), 16);
		meta_stride_uv = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 16), 64);
		meta_scanline_uv = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 8), 16);
		break;
	case V4L2_PIX_FMT_QC10C:
		stride_y = ALIGN(width * 4 / 3, 256);
		scanline_y = ALIGN(height, 16);
		stride_uv = ALIGN(width * 4 / 3, 256);
		scanline_uv = ALIGN((height + 1) >> 1, 16);
		meta_stride_y = ALIGN(DIV_ROUND_UP(width, 48), 64);
		meta_scanline_y = ALIGN(DIV_ROUND_UP(height, 4), 16);
		meta_stride_uv = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 24), 64);
		meta_scanline_uv = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16);
		break;
	default:
		return 0;
	}

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;
	payload[2] = meta_stride_y << 16 | meta_scanline_y;
	payload[3] = meta_stride_uv << 16 | meta_scanline_uv;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_UBWC_STRIDE_SCANLINE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ARRAY,
						  &payload[0],
						  sizeof(u32) * 4);
}

static int iris_hfi_gen2_set_tier(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 tier = inst->fw_caps[TIER].value;

	tier = (inst->codec == V4L2_PIX_FMT_AV1) ? inst->fw_caps[TIER_AV1].value :
							inst->fw_caps[TIER].value;
	inst_hfi_gen2->src_subcr_params.tier = tier;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_TIER,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &tier,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_frame_rate(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	u32 frame_rate = inst->frame_rate << 16;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_FRAME_RATE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_Q16,
						  &frame_rate,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_film_grain(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 film_grain = inst->fw_caps[FILM_GRAIN].value;

	inst_hfi_gen2->src_subcr_params.film_grain = film_grain;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_AV1_FILM_GRAIN_PRESENT,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &film_grain,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_super_block(struct iris_inst *inst, u32 plane)
{
	u32 port = iris_hfi_gen2_get_port(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 super_block = inst->fw_caps[SUPER_BLOCK].value;

	inst_hfi_gen2->src_subcr_params.super_block = super_block;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_AV1_SUPER_BLOCK_ENABLED,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &super_block,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_decode_order_output(struct iris_inst *inst, u32 plane)
{
	struct platform_inst_fw_cap *cap = &inst->fw_caps[DISPLAY_DELAY_ENABLE];
	u32 decode_order = cap->value;

	/* Leave the firmware default unchanged unless userspace sets the control. */
	if (!(cap->flags & CAP_FLAG_CLIENT_SET))
		return 0;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_DECODE_ORDER_OUTPUT,
						  HFI_HOST_FLAGS_NONE,
						  HFI_PORT_BITSTREAM,
						  HFI_PAYLOAD_U32,
						  &decode_order, sizeof(decode_order));
}

static int iris_hfi_gen2_session_set_config_params(struct iris_inst *inst, u32 plane)
{
	const struct iris_firmware_data *fdata = inst->core->iris_firmware_data;
	u32 config_params_size = 0, i, j;
	const u32 *config_params = NULL;
	int ret;

	static const struct iris_hfi_prop_type_handle prop_type_handle_arr[] = {
		{HFI_PROP_RAW_RESOLUTION,             iris_hfi_gen2_set_raw_resolution         },
		{HFI_PROP_BITSTREAM_RESOLUTION,       iris_hfi_gen2_set_bitstream_resolution   },
		{HFI_PROP_CROP_OFFSETS,               iris_hfi_gen2_set_crop_offsets           },
		{HFI_PROP_CODED_FRAMES,               iris_hfi_gen2_set_coded_frames           },
		{HFI_PROP_LUMA_CHROMA_BIT_DEPTH,      iris_hfi_gen2_set_bit_depth              },
		{HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT, iris_hfi_gen2_set_min_output_count       },
		{HFI_PROP_PIC_ORDER_CNT_TYPE,         iris_hfi_gen2_set_picture_order_count    },
		{HFI_PROP_SIGNAL_COLOR_INFO,          iris_hfi_gen2_set_colorspace             },
		{HFI_PROP_PROFILE,                    iris_hfi_gen2_set_profile                },
		{HFI_PROP_LEVEL,                      iris_hfi_gen2_set_level                  },
		{HFI_PROP_OPB_ENABLE,                 iris_hfi_gen2_set_opb_enable             },
		{HFI_PROP_COLOR_FORMAT,               iris_hfi_gen2_set_colorformat            },
		{HFI_PROP_LINEAR_STRIDE_SCANLINE,     iris_hfi_gen2_set_linear_stride_scanline },
		{HFI_PROP_UBWC_STRIDE_SCANLINE,       iris_hfi_gen2_set_ubwc_stride_scanline   },
		{HFI_PROP_TIER,                       iris_hfi_gen2_set_tier                   },
		{HFI_PROP_FRAME_RATE,                 iris_hfi_gen2_set_frame_rate             },
		{HFI_PROP_AV1_FILM_GRAIN_PRESENT,     iris_hfi_gen2_set_film_grain             },
		{HFI_PROP_AV1_SUPER_BLOCK_ENABLED,    iris_hfi_gen2_set_super_block            },
		{HFI_PROP_DECODE_ORDER_OUTPUT,        iris_hfi_gen2_set_decode_order_output    },
