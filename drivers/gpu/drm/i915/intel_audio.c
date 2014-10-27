/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/kernel.h>

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include "i915_drv.h"

static const struct {
	int clock;
	u32 config;
} hdmi_audio_clock[] = {
	{ DIV_ROUND_UP(25200 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_25175 },
	{ 25200, AUD_CONFIG_PIXEL_CLOCK_HDMI_25200 }, /* default per bspec */
	{ 27000, AUD_CONFIG_PIXEL_CLOCK_HDMI_27000 },
	{ 27000 * 1001 / 1000, AUD_CONFIG_PIXEL_CLOCK_HDMI_27027 },
	{ 54000, AUD_CONFIG_PIXEL_CLOCK_HDMI_54000 },
	{ 54000 * 1001 / 1000, AUD_CONFIG_PIXEL_CLOCK_HDMI_54054 },
	{ DIV_ROUND_UP(74250 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_74176 },
	{ 74250, AUD_CONFIG_PIXEL_CLOCK_HDMI_74250 },
	{ DIV_ROUND_UP(148500 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_148352 },
	{ 148500, AUD_CONFIG_PIXEL_CLOCK_HDMI_148500 },
};

/* get AUD_CONFIG_PIXEL_CLOCK_HDMI_* value for mode */
static u32 audio_config_hdmi_pixel_clock(struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_audio_clock); i++) {
		if (mode->clock == hdmi_audio_clock[i].clock)
			break;
	}

	if (i == ARRAY_SIZE(hdmi_audio_clock)) {
		DRM_DEBUG_KMS("HDMI audio pixel clock setting for %d not found, falling back to defaults\n", mode->clock);
		i = 1;
	}

	DRM_DEBUG_KMS("Configuring HDMI audio for pixel clock %d (0x%08x)\n",
		      hdmi_audio_clock[i].clock,
		      hdmi_audio_clock[i].config);

	return hdmi_audio_clock[i].config;
}

static bool intel_eld_uptodate(struct drm_connector *connector,
			       int reg_eldv, uint32_t bits_eldv,
			       int reg_elda, uint32_t bits_elda,
			       int reg_edid)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t tmp;
	int i;

	tmp = I915_READ(reg_eldv);
	tmp &= bits_eldv;

	if (!tmp)
		return false;

	tmp = I915_READ(reg_elda);
	tmp &= ~bits_elda;
	I915_WRITE(reg_elda, tmp);

	for (i = 0; i < eld[2]; i++)
		if (I915_READ(reg_edid) != *((uint32_t *)eld + i))
			return false;

	return true;
}

static void g4x_audio_codec_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	uint32_t eldv, tmp;

	DRM_DEBUG_KMS("Disable audio codec\n");

	tmp = I915_READ(G4X_AUD_VID_DID);
	if (tmp == INTEL_AUDIO_DEVBLC || tmp == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	/* Invalidate ELD */
	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp &= ~eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);
}

static void g4x_audio_codec_enable(struct drm_connector *connector,
				   struct intel_encoder *encoder,
				   struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t tmp;
	int len, i;

	tmp = I915_READ(G4X_AUD_VID_DID);
	if (tmp == INTEL_AUDIO_DEVBLC || tmp == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	if (intel_eld_uptodate(connector,
			       G4X_AUD_CNTL_ST, eldv,
			       G4X_AUD_CNTL_ST, G4X_ELD_ADDR_MASK,
			       G4X_HDMIW_HDMIEDID))
		return;

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp &= ~(eldv | G4X_ELD_ADDR_MASK);
	len = (tmp >> 9) & 0x1f;		/* ELD buffer size */
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);

	len = min_t(int, eld[2], len);
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(G4X_HDMIW_HDMIEDID, *((uint32_t *)eld + i));

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp |= eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);
}

static void hsw_audio_codec_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum pipe pipe = intel_crtc->pipe;
	uint32_t tmp;

	DRM_DEBUG_KMS("Disable audio codec on pipe %c\n", pipe_name(pipe));

	/* Disable timestamps */
	tmp = I915_READ(HSW_AUD_CFG(pipe));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp |= AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_UPPER_N_MASK;
	tmp &= ~AUD_CONFIG_LOWER_N_MASK;
	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	I915_WRITE(HSW_AUD_CFG(pipe), tmp);

	/* Invalidate ELD */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp &= ~(AUDIO_ELD_VALID_A << (pipe * 4));
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);
}

static void hsw_audio_codec_enable(struct drm_connector *connector,
				   struct intel_encoder *encoder,
				   struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum pipe pipe = intel_crtc->pipe;
	const uint8_t *eld = connector->eld;
	uint32_t tmp;
	int len, i;

	DRM_DEBUG_KMS("Enable audio codec on pipe %c, %u bytes ELD\n",
		      pipe_name(pipe), eld[2]);

	/* Enable audio presence detect, invalidate ELD */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp |= AUDIO_OUTPUT_ENABLE_A << (pipe * 4);
	tmp &= ~(AUDIO_ELD_VALID_A << (pipe * 4));
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);

	/*
	 * FIXME: We're supposed to wait for vblank here, but we have vblanks
	 * disabled during the mode set. The proper fix would be to push the
	 * rest of the setup into a vblank work item, queued here, but the
	 * infrastructure is not there yet.
	 */

	/* Reset ELD write address */
	tmp = I915_READ(HSW_AUD_DIP_ELD_CTRL(pipe));
	tmp &= ~IBX_ELD_ADDRESS_MASK;
	I915_WRITE(HSW_AUD_DIP_ELD_CTRL(pipe), tmp);

	/* Up to 84 bytes of hw ELD buffer */
	len = min_t(int, eld[2], 21);
	for (i = 0; i < len; i++)
		I915_WRITE(HSW_AUD_EDID_DATA(pipe), *((uint32_t *)eld + i));

	/* ELD valid */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp |= AUDIO_ELD_VALID_A << (pipe * 4);
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);

	/* Enable timestamps */
	tmp = I915_READ(HSW_AUD_CFG(pipe));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	else
		tmp |= audio_config_hdmi_pixel_clock(mode);
	I915_WRITE(HSW_AUD_CFG(pipe), tmp);
}

static void ilk_audio_codec_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(&encoder->base);
	enum port port = intel_dig_port->port;
	enum pipe pipe = intel_crtc->pipe;
	uint32_t tmp, eldv;
	int aud_config;
	int aud_cntrl_st2;

	DRM_DEBUG_KMS("Disable audio codec on port %c, pipe %c\n",
		      port_name(port), pipe_name(pipe));

	if (HAS_PCH_IBX(dev_priv->dev)) {
		aud_config = IBX_AUD_CFG(pipe);
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		aud_config = VLV_AUD_CFG(pipe);
		aud_cntrl_st2 = VLV_AUD_CNTL_ST2;
	} else {
		aud_config = CPT_AUD_CFG(pipe);
		aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	}

	/* Disable timestamps */
	tmp = I915_READ(aud_config);
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp |= AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_UPPER_N_MASK;
	tmp &= ~AUD_CONFIG_LOWER_N_MASK;
	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	I915_WRITE(aud_config, tmp);

	if (WARN_ON(!port)) {
		eldv = IBX_ELD_VALIDB;
		eldv |= IBX_ELD_VALIDB << 4;
		eldv |= IBX_ELD_VALIDB << 8;
	} else {
		eldv = IBX_ELD_VALIDB << ((port - 1) * 4);
	}

	/* Invalidate ELD */
	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);
}

static void ilk_audio_codec_enable(struct drm_connector *connector,
				   struct intel_encoder *encoder,
				   struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(&encoder->base);
	enum port port = intel_dig_port->port;
	enum pipe pipe = intel_crtc->pipe;
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t tmp;
	int len, i;
	int hdmiw_hdmiedid;
	int aud_config;
	int aud_cntl_st;
	int aud_cntrl_st2;

	DRM_DEBUG_KMS("Enable audio codec on port %c, pipe %c, %u bytes ELD\n",
		      port_name(port), pipe_name(pipe), eld[2]);

	/*
	 * FIXME: We're supposed to wait for vblank here, but we have vblanks
	 * disabled during the mode set. The proper fix would be to push the
	 * rest of the setup into a vblank work item, queued here, but the
	 * infrastructure is not there yet.
	 */

	if (HAS_PCH_IBX(connector->dev)) {
		hdmiw_hdmiedid = IBX_HDMIW_HDMIEDID(pipe);
		aud_config = IBX_AUD_CFG(pipe);
		aud_cntl_st = IBX_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else if (IS_VALLEYVIEW(connector->dev)) {
		hdmiw_hdmiedid = VLV_HDMIW_HDMIEDID(pipe);
		aud_config = VLV_AUD_CFG(pipe);
		aud_cntl_st = VLV_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = VLV_AUD_CNTL_ST2;
	} else {
		hdmiw_hdmiedid = CPT_HDMIW_HDMIEDID(pipe);
		aud_config = CPT_AUD_CFG(pipe);
		aud_cntl_st = CPT_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	}

	if (WARN_ON(!port)) {
		eldv = IBX_ELD_VALIDB;
		eldv |= IBX_ELD_VALIDB << 4;
		eldv |= IBX_ELD_VALIDB << 8;
	} else {
		eldv = IBX_ELD_VALIDB << ((port - 1) * 4);
	}

	/* Invalidate ELD */
	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	/* Reset ELD write address */
	tmp = I915_READ(aud_cntl_st);
	tmp &= ~IBX_ELD_ADDRESS_MASK;
	I915_WRITE(aud_cntl_st, tmp);

	/* Up to 84 bytes of hw ELD buffer */
	len = min_t(int, eld[2], 21);
	for (i = 0; i < len; i++)
		I915_WRITE(hdmiw_hdmiedid, *((uint32_t *)eld + i));

	/* ELD valid */
	tmp = I915_READ(aud_cntrl_st2);
	tmp |= eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	/* Enable timestamps */
	tmp = I915_READ(aud_config);
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	else
		tmp |= audio_config_hdmi_pixel_clock(mode);
	I915_WRITE(aud_config, tmp);
}

/**
 * intel_audio_codec_enable - Enable the audio codec for HD audio
 * @intel_encoder: encoder on which to enable audio
 *
 * The enable sequences may only be performed after enabling the transcoder and
 * port, and after completed link training.
 */
void intel_audio_codec_enable(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct intel_crtc *crtc = to_intel_crtc(encoder->crtc);
	struct drm_display_mode *mode = &crtc->config.adjusted_mode;
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	connector = drm_select_eld(encoder, mode);
	if (!connector)
		return;

	DRM_DEBUG_DRIVER("ELD on [CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
			 connector->base.id,
			 connector->name,
			 connector->encoder->base.id,
			 connector->encoder->name);

	/* ELD Conn_Type */
	connector->eld[5] &= ~(3 << 2);
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT))
		connector->eld[5] |= (1 << 2);

	connector->eld[6] = drm_av_sync_delay(connector, mode) / 2;

	if (dev_priv->display.audio_codec_enable)
		dev_priv->display.audio_codec_enable(connector, intel_encoder, mode);
}

/**
 * intel_audio_codec_disable - Disable the audio codec for HD audio
 * @encoder: encoder on which to disable audio
 *
 * The disable sequences must be performed before disabling the transcoder or
 * port.
 */
void intel_audio_codec_disable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->display.audio_codec_disable)
		dev_priv->display.audio_codec_disable(encoder);
}

/**
 * intel_init_audio - Set up chip specific audio functions
 * @dev: drm device
 */
void intel_init_audio(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_G4X(dev)) {
		dev_priv->display.audio_codec_enable = g4x_audio_codec_enable;
		dev_priv->display.audio_codec_disable = g4x_audio_codec_disable;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.audio_codec_enable = ilk_audio_codec_enable;
		dev_priv->display.audio_codec_disable = ilk_audio_codec_disable;
	} else if (IS_HASWELL(dev) || INTEL_INFO(dev)->gen >= 8) {
		dev_priv->display.audio_codec_enable = hsw_audio_codec_enable;
		dev_priv->display.audio_codec_disable = hsw_audio_codec_disable;
	} else if (HAS_PCH_SPLIT(dev)) {
		dev_priv->display.audio_codec_enable = ilk_audio_codec_enable;
		dev_priv->display.audio_codec_disable = ilk_audio_codec_disable;
	}
}
