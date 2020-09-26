// High quality GPU accelerated filters for vapoursynth using the movit library
// Copyright (C) 2020 Martin Sandsmark <martin.sandsmark@kde.org>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// this isn't finished
// this probably doesn't even build
// and I don't understand any of what I'm doing
//

#include <VapourSynth.h>
#include <VSHelper.h>

#include <movit/effect.h>
#include <movit/effect_chain.h>
#include <movit/resource_pool.h>
#include <movit/ycbcr_input.h>
#include <movit/ycbcr_422interleaved_input.h>
#include <movit/flat_input.h>

// all filters!!
#include <movit/alpha_division_effect.h>
#include <movit/alpha_multiplication_effect.h>
#include <movit/blur_effect.h>
//#include <movit/colorspace_conversion_effect.h>
#include <movit/complex_modulate_effect.h>
#include <movit/deconvolution_sharpen_effect.h>
#include <movit/deinterlace_effect.h>
#include <movit/diffusion_effect.h>
//#include <movit/dither_effect.h>
//#include <movit/fft_convolution_effect.h>
#include <movit/fft_pass_effect.h>
//#include <movit/gamma_compression_effect.h>
//#include <movit/gamma_expansion_effect.h>
#include <movit/glow_effect.h>
#include <movit/lift_gamma_gain_effect.h>
#include <movit/luma_mix_effect.h>
#include <movit/mirror_effect.h>
#include <movit/mix_effect.h>
#include <movit/multiply_effect.h>
#include <movit/overlay_effect.h>
#include <movit/padding_effect.h>
#include <movit/resample_effect.h>
#include <movit/resize_effect.h>
#include <movit/sandbox_effect.h>
#include <movit/saturation_effect.h>
#include <movit/slice_effect.h>
#include <movit/unsharp_mask_effect.h>
#include <movit/vignette_effect.h>
#include <movit/white_balance_effect.h>
//#include <movit/ycbcr_conversion_effect.h>
//

#include <EGL/egl.h>

#include <memory>
#include <vector>
#include <istream>
#include <string>
#include <iostream>

namespace {

struct FilterData {
    VSNodeRef *node = nullptr;
    const VSVideoInfo *vi = nullptr;
    EGLDisplay eglDpy;
    EGLSurface eglSurf;
    EGLContext eglCtx;

    int pbufferWidth = 9;
    int pbufferHeight = 9;
    std::shared_ptr<movit::EffectChain> chain = nullptr;
    movit::FlatInput *flatInput = nullptr;
    movit::YCbCrInput *ycbcrInput = nullptr;

    GLenum datatype = GL_UNSIGNED_BYTE;
};

} // anonymous namespace

static std::vector<std::string> stringSplit(const std::string &string, const char delimiter)
{
    if (string.find(delimiter) == std::string::npos) {
        return {string};
    }

    std::vector<std::string> ret;
    std::istringstream stream(string);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        if (part.empty()) {
            continue;
        }

        ret.push_back(part);
    }
    if (ret.empty()) {
        return {string};
    }
    return ret;
}

static void VS_CC filterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    FilterData *d = (FilterData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC filterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    FilterData *d = (FilterData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady) {
        return nullptr;
    }
    const VSFrameRef *frame = vsapi->getFrameFilter(n, d->node, frameCtx);
    // TODO: can the size be different for different planes? u never know, probably, and that won't go well
    const unsigned width = vsapi->getFrameWidth(frame, 0); // todo when sending as well
    const unsigned height = vsapi->getFrameHeight(frame, 0); // todo when sending as well

    eglMakeCurrent(d->eglDpy, d->eglSurf, d->eglSurf, d->eglCtx);
    if (d->ycbcrInput) {
        // TODO pitch
        if (width != d->ycbcrInput->get_width()) {
            d->ycbcrInput->set_width(width);
        }
        if (height != d->ycbcrInput->get_height()) {
            d->ycbcrInput->set_height(height);
        }
        for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
            switch(d->datatype) {
            case GL_UNSIGNED_BYTE:
                d->ycbcrInput->set_pixel_data(plane, vsapi->getReadPtr(frame, plane));
                break;
            case GL_UNSIGNED_SHORT:
                d->ycbcrInput->set_pixel_data(plane, reinterpret_cast<const uint16_t*>(vsapi->getReadPtr(frame, plane)));
                break;
            case GL_UNSIGNED_INT_2_10_10_10_REV:
                d->ycbcrInput->set_pixel_data(plane, reinterpret_cast<const uint32_t*>(vsapi->getReadPtr(frame, plane)));
                break;
            }
        }
    } else {
        assert(d->vi->format->numPlanes == 1);
        // TODO pitch
        if (width != d->flatInput->get_width()) {
            d->flatInput->set_width(width);
        }
        if (height != d->flatInput->get_height()) {
            d->flatInput->set_height(height);
        }
        switch(d->datatype) {
        case GL_UNSIGNED_BYTE:
            d->flatInput->set_pixel_data(vsapi->getReadPtr(frame, 0));
            break;
        case GL_UNSIGNED_SHORT:
            d->flatInput->set_pixel_data(reinterpret_cast<const uint16_t*>(vsapi->getReadPtr(frame, 0)));
            break;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            assert(false);
        //    d->flatInput->set_pixel_data(reinterpret_cast<const uint32_t*>(vsapi->getReadPtr(frame, 0)));
            break;
        }
    }
    std::vector<movit::EffectChain::DestinationTexture> textures;
    for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
        movit::EffectChain::DestinationTexture texture;
        texture.texnum = d->chain->get_resource_pool()->create_2d_texture(d->datatype, width, height);
        texture.format = d->datatype;
        textures.push_back(std::move(texture));
    }
    d->chain->render_to_texture(textures, width, height);

    VSFrameRef *dst = vsapi->copyFrame(frame, core);
    for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
        glBindTexture(GL_TEXTURE_2D, textures[plane].texnum);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, d->datatype, vsapi->getWritePtr(dst, plane));//todo need to get proper output format
    }

    //switch(d->datatype) {
    //case GL_UNSIGNED_BYTE: {
    //    glReadPixels(0, 0, d->pbufferWidth, d->pbufferHeight, GL_RGB, GL_UNSIGNED_BYTE, vsapi->getWritePtr(dst, 0));
    //    break;
    //}
    //case GL_UNSIGNED_SHORT:
    //    glReadPixels(0, 0, d->pbufferWidth, d->pbufferHeight, GL_RGB, GL_UNSIGNED_SHORT, reinterpret_cast<uint16_t*>(vsapi->getWritePtr(dst, 0)));
    //    break;
    //case GL_UNSIGNED_INT_2_10_10_10_REV:
    //    glReadPixels(0, 0, d->pbufferWidth, d->pbufferHeight, GL_RGB, GL_UNSIGNED_INT_2_10_10_10_REV, reinterpret_cast<uint32_t*>(vsapi->getWritePtr(dst, 0)));
    //    break;
    //}

    return dst;
}

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    FilterData *d = (FilterData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

// todo error check and handle and shit
static bool createGlContext(FilterData *d)
{
    d->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    EGLint major, minor;
    eglInitialize(d->eglDpy, &major, &minor);


    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLConfig eglCfg;
    EGLint numConfigs;
    eglChooseConfig(d->eglDpy, configAttribs, &eglCfg, 1, &numConfigs);

    static const EGLint pbufferAttribs[] = {
        EGL_WIDTH, d->pbufferWidth,
        EGL_HEIGHT, d->pbufferHeight,
        EGL_NONE,
    };
    d->eglSurf = eglCreatePbufferSurface(d->eglDpy, eglCfg, pbufferAttribs);

    eglBindAPI(EGL_OPENGL_API);

    d->eglCtx = eglCreateContext(d->eglDpy, eglCfg, EGL_NO_CONTEXT, NULL);

    return true;
}

static bool addEffect(const std::string &definition, FilterData *d, std::string *error)
{
    std::vector<std::string> nameAndArgs = stringSplit(definition, ':');
    if (nameAndArgs.size() > 2) {
        *error = "Invalid filter definition: " + definition;
        return false;
    }
    const std::string &name = nameAndArgs[0];

    std::unique_ptr<movit::Effect> effect;

    if (name == "AlphaDivision") {
        effect.reset(new movit::AlphaDivisionEffect);
    } else if (name == "AlphaMultiplication") {
        effect.reset(new movit::AlphaMultiplicationEffect);
    } else if (name == "Blur") {
        effect.reset(new movit::BlurEffect);
    //} else if (name == "SingleBlurPass") {
    //    effect.reset(new movit::SingleBlurPassEffect);
    //} else if (name == "ColorspaceConversion") {
    //    effect.reset(new movit::ColorspaceConversionEffect);
    } else if (name == "ComplexModulate") {
        effect.reset(new movit::ComplexModulateEffect);
    } else if (name == "DeconvolutionSharpen") {
        effect.reset(new movit::DeconvolutionSharpenEffect);
    } else if (name == "Deinterlace") {
        effect.reset(new movit::DeinterlaceEffect);
    } else if (name == "DeinterlaceCompute") {
        effect.reset(new movit::DeinterlaceComputeEffect);
    } else if (name == "Diffusion") {
        effect.reset(new movit::DiffusionEffect);
    } else if (name == "OverlayMatte") {
        effect.reset(new movit::OverlayMatteEffect);
    //} else if (name == "Dither") { TODO: expose set_dither_bits
    //    effect.reset(new movit::DitherEffect);
    //} else if (name == "FFTConvolution") { TODO: needs parameters
    //    effect.reset(new movit::FFTConvolutionEffect);
    } else if (name == "FFTPass") {
        effect.reset(new movit::FFTPassEffect);
    //} else if (name == "GammaCompression") {
    //    effect.reset(new movit::GammaCompressionEffect);
    //} else if (name == "GammaExpansion") {
    //    effect.reset(new movit::GammaExpansionEffect);
    } else if (name == "Glow") {
        effect.reset(new movit::GlowEffect);
    } else if (name == "HighlightCutoff") {
        effect.reset(new movit::HighlightCutoffEffect);
    } else if (name == "LiftGammaGain") {
        effect.reset(new movit::LiftGammaGainEffect);
    } else if (name == "LumaMix") {
        effect.reset(new movit::LumaMixEffect);
    } else if (name == "Mirror") {
        effect.reset(new movit::MirrorEffect);
    } else if (name == "Mix") {
        effect.reset(new movit::MixEffect);
    } else if (name == "Multiply") {
        effect.reset(new movit::MultiplyEffect);
    } else if (name == "Overlay") {
        effect.reset(new movit::OverlayEffect);
    } else if (name == "Padding") {
        effect.reset(new movit::PaddingEffect);
    } else if (name == "IntegralPadding") {
        effect.reset(new movit::IntegralPaddingEffect);
    } else if (name == "IntegralPadding") {
        effect.reset(new movit::IntegralPaddingEffect);
    } else if (name == "Resample") {
        effect.reset(new movit::ResampleEffect);
    } else if (name == "SingleResamplePass") {
        effect.reset(new movit::SingleResamplePassEffect(nullptr)); // TODO: should probably set a proper parent
    } else if (name == "Resize") {
        effect.reset(new movit::ResizeEffect);
    } else if (name == "Resize") {
        effect.reset(new movit::ResizeEffect);
    } else if (name == "Sandbox") {
        effect.reset(new movit::SandboxEffect);
    } else if (name == "Sandbox") {
        effect.reset(new movit::SandboxEffect);
    } else if (name == "Saturation") {
        effect.reset(new movit::SaturationEffect);
    } else if (name == "Slice") {
        effect.reset(new movit::SliceEffect);
    } else if (name == "UnsharpMask") {
        effect.reset(new movit::UnsharpMaskEffect);
    } else if (name == "Vignette") {
        effect.reset(new movit::VignetteEffect);
    } else if (name == "WhiteBalance") {
        effect.reset(new movit::WhiteBalanceEffect);
    //} else if (name == "YCbCrConversion") {
    //    effect.reset(new movit::YCbCrConversionEffect);
    } else {
        *error = "Unknown effect name '" + name + "'";
        return false;
    }

    for (const std::string &arg : stringSplit(nameAndArgs[1], ':')) {
        std::vector<std::string> nameAndValue = stringSplit(arg, '=');
        if (nameAndValue.size() != 2) {
            *error = "Invalid argument: '" + arg + "'";
            return false;
        }
        std::vector<std::string> stringValues = stringSplit(nameAndValue[1], ',');

        std::vector<float> values;
        for (const std::string str : stringValues) {
            float value = 0.f;
            try {
                value = std::stof(str);
            } catch (const std::invalid_argument &err) {
                *error = "Unknown value '" + str + "' for " + arg;
                return false;
            }
            values.push_back(value);
        }

        const std::string &argName = nameAndValue[0];
        switch(values.size()) {
        case 1:
            if (!effect->set_float(argName, values[0])) {
                *error = "Invalid value for " + arg;
                return false;
            }
            break;
        case 2:
            if (!effect->set_vec2(argName, values.data())) {
                *error = "Invalid value for " + arg;
                return false;
            }
            break;
        case 3:
            if (!effect->set_vec3(argName, values.data())) {
                *error = "Invalid value for " + arg;
                return false;
            }
            break;
        case 4:
            if (!effect->set_vec4(argName, values.data())) {
                *error = "Invalid value for " + arg;
                return false;
            }
            break;
        default:
            *error = "Invalid number of values for argument " + arg;
            return false;
        }
    }

    d->chain->add_effect(effect.release());

    return true;
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FilterData d;
    std::string error;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    int err;
    std::string chainDefinition(vsapi->propGetData(in, "chain", 0, &err));
    if (err || chainDefinition.empty()) {
        error = "Invalid chain defintion: " + chainDefinition;
        vsapi->setError(out, error.c_str());
        return;
    }


    d.pbufferWidth = int64ToIntS(vsapi->propGetInt(in, "contextWidth", 0, &err));
    if (err || d.pbufferWidth <= 0) {
        error = "Invalid contextWidth";
        vsapi->setError(out, error.c_str());
        return;
    }
    d.pbufferHeight = int64ToIntS(vsapi->propGetInt(in, "contextHeight", 0, &err));
    if (err || d.pbufferHeight <= 0) {
        error = "Invalid contextHeight";
        vsapi->setError(out, error.c_str());
        return;
    }

    if (!createGlContext(&d)) {
        error = "Failed to create gl context";
        vsapi->setError(out, error.c_str());
        return;
    }

    eglMakeCurrent(d.eglDpy, d.eglSurf, d.eglSurf, d.eglCtx);

    d.chain = std::make_shared<movit::EffectChain>(d.pbufferWidth, d.pbufferHeight);

    movit::ImageFormat inout_format;
    std::string colorspace(vsapi->propGetData(in, "colorspace", 0, &err));
    if (colorspace == "sRGB") {
        inout_format.color_space = movit::COLORSPACE_sRGB;
    } else if (colorspace == "REC_709") {
        inout_format.color_space = movit::COLORSPACE_REC_709;
    } else if (colorspace == "REC_601_525") {
        inout_format.color_space = movit::COLORSPACE_REC_601_525;
    } else if (colorspace == "REC_601_625") {
        inout_format.color_space = movit::COLORSPACE_REC_601_625;
    } else if (colorspace == "XYZ") {
        inout_format.color_space = movit::COLORSPACE_XYZ;
    } else {
        inout_format.color_space = movit::COLORSPACE_sRGB;
    }

    std::string gammacurve(vsapi->propGetData(in, "gammacurve", 0, &err));
    if (gammacurve == "LINEAR") {
        inout_format.gamma_curve = movit::GAMMA_LINEAR;
    } else if (gammacurve == "sRGB") {
        inout_format.gamma_curve = movit::GAMMA_sRGB;
    } else if (gammacurve == "REC_601" || gammacurve == "REC_709" || gammacurve == "REC_2020_10_BIT") {
        inout_format.gamma_curve = movit::GAMMA_REC_601;
    } else if (gammacurve == "REC_2020_12_BIT") {
        inout_format.gamma_curve = movit::GAMMA_REC_2020_12_BIT;
    } else {
        inout_format.gamma_curve = movit::GAMMA_LINEAR;
    }

    if (d.vi->format->sampleType == stInteger) {
        switch(d.vi->format->bytesPerSample) {
        case 1:
            d.datatype = GL_UNSIGNED_BYTE;
            break;
        case 2:
            d.datatype = GL_UNSIGNED_SHORT;
            break;
        case 4:
            d.datatype = GL_UNSIGNED_INT_2_10_10_10_REV;
            break;
        default:
            error = "Unhandled bytes per sample";
            vsapi->setError(out, error.c_str());
            return;
        }
    } else if (d.vi->format->sampleType == stFloat) {
        switch(d.vi->format->bytesPerSample) {
        case 2:
            d.datatype = GL_HALF_FLOAT;
            break;
        case 4:
            d.datatype = GL_FLOAT;
            break;
        default:
            error = "Unhandled bytes per sample";
            vsapi->setError(out, error.c_str());
            return;
        }
    } else {
        error = "Unhandled sample type";
        vsapi->setError(out, error.c_str());
        return;
    }

    switch(d.vi->format->colorFamily) {
    case cmRGB: {
        d.flatInput = new movit::FlatInput(inout_format, movit::FORMAT_BGRA_POSTMULTIPLIED_ALPHA, d.datatype, d.pbufferWidth, d.pbufferHeight);
        d.chain->add_input(d.flatInput);
        break;
    }
    case cmYUV: {
        //// todo fetch and check all the parameters
        movit::YCbCrFormat format; /////

        if (d.vi->format->id == pfYUV422P8) {
            assert(false);
            //d.ycbcrInput = new movit::YCbCr422InterleavedInput(inout_format, format, d.pbufferWidth, d.pbufferHeight);
        } else {
            d.ycbcrInput = new movit::YCbCrInput(inout_format, format, d.pbufferWidth, d.pbufferHeight, movit::YCBCR_INPUT_PLANAR, d.datatype);
        }
        d.chain->add_input(d.ycbcrInput);
        break;
    }
    }


    // example "Saturation:saturation=0.7 LiftGammaGain:gain=0.8,1.0,1.0"
    // don't use something similar to the ffmpeg filtergraph syntax because
    // that is braindead
    std::vector<std::string> definitions = stringSplit(chainDefinition, ' ');
    for (const std::string &definition : definitions) {
        std::string error;
        if (!addEffect(definition, &d, &error)) {
            vsapi->setError(out, error.c_str());
            return;
        }
    }
    d.chain->add_output(inout_format, movit::OUTPUT_ALPHA_FORMAT_POSTMULTIPLIED);
    d.chain->finalize();

    vsapi->createFilter(
            in,
            out,
            "Movit",
            filterInit,
            filterGetFrame,
            filterFree,
            fmParallel,
            0,
            new FilterData(std::move(d)),
            core
        );
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("net.sesse.movit", "movit", "GPU accelerated effects and stuff", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Movit"
            ,
            "clip:clip;"
            "contextWidth:int;"
            "contextHeight:int;"
            "chain:data;"
            "colorspace:data:opt;"
            "gammacurve:data:opt;"
            "ycbcr_fullrange:int:opt;"
            "ycbcr_num_levels:int:opt;"
            "ycbcr_chroma_subsampling_x:int:opt;"
            "ycbcr_chroma_subsampling_y:int:opt;"
            "ycbcr_cb_x_position:float:opt;"
            "ycbcr_cb_y_position:float:opt;"
            "ycbcr_cr_y_position:float:opt;"
            "ycbcr_cr_x_position:float:opt;"
            ,
            filterCreate
            ,
            0
            ,
            plugin
    );
}
