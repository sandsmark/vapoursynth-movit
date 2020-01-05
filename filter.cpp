// this isn't finished
// this probably doesn't even build
// and I don't understand any of what I'm doing
//

#include <VapourSynth.h>
#include <VSHelper.h>

#include <EGL/egl.h>

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

static const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE
};

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
        return;
    }
    if (activationReason != arAllFramesReady) {
        return nullptr
    }
    const VSFrameRef *frame = vsapi->getFrameFilter(n, d->node, frameCtx);

    eglMakeCurrent(d->eglDpy, d->eglSurf, d->eglSurf, d->eglCtx);
    if (d->ycbcrInput) {
        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            switch(d->datatype) {
            case GL_UNSIGNED_BYTE:
                d->ycbcrInput->set_pixel_data(plane, vsapi->getReadPtr(frame));
                break;
            case GL_UNSIGNED_SHORT:
                d->ycbcrInput->set_pixel_data(plane, reinterpret_cast<const uint16_t*>(vsapi->getReadPtr(frame)));
                break;
            case GL_UNSIGNED_INT_2_10_10_10_REV:
                d->ycbcrInput->set_pixel_data(plane, reinterpret_cast<const uint32_t*>(vsapi->getReadPtr(frame)));
                break;
            }
        }
    } else {
        assert(d->vi.format->numPlanes == 1);
        switch(d->datatype) {
        case GL_UNSIGNED_BYTE:
            d->flatInput->set_pixel_data(plane, vsapi->getReadPtr(frame));
            break;
        case GL_UNSIGNED_SHORT:
            d->flatInput->set_pixel_data(reinterpret_cast<const uint16_t*>(vsapi->getReadPtr(frame)));
            break;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            d->flatInput->set_pixel_data(reinterpret_cast<const uint32_t*>(vsapi->getReadPtr(frame)));
            break;
        }
    }

    return frame;
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

    EGLint numConfigs;
    EGLConfig eglCfg;

    eglChooseConfig(d->eglDpy, configAttribs, &eglCfg, 1, &numConfigs);

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, d->pbufferWidth,
        EGL_HEIGHT, d->pbufferHeight,
        EGL_NONE,
    };

    d->eglSurf = eglCreatePbufferSurface(d->eglDpy, eglCfg, pbufferAttribs);
    eglBindAPI(EGL_OPENGL_API);
    d->eglCtx = eglCreateContext(d->eglDpy, eglCfg, EGL_NO_CONTEXT, NULL);

    return true;
}

static bool addEffect(const std::string &definition, FilterData *d)
{
    std::vector<std::string> nameAndArgs = stringSplit(definition, ':');
    if (nameAndArgs.size() > 2) {
        std::cerr << "Invalid filter definition" << std::endl;
        return false;
    }
    const std::string &name = nameAndArgs[0];

    std::unique_ptr<movit::Effect> effect;

    if (name == "AlphaDivision") {
        effect = new movit::AlphaDivisionEffect;
    } else if (name == "AlphaMultiplication") {
        effect = new movit::AlphaMultiplicationEffect;
    } else if (name == "Blur") {
        effect = new movit::BlurEffect;
    } else if (name == "SingleBlurPass") {
        effect = new movit::SingleBlurPassEffect;
    } else if (name == "ColorspaceConversion") {
        effect = new movit::ColorspaceConversionEffect;
    } else if (name == "ComplexModulate") {
        effect = new movit::ComplexModulateEffect;
    } else if (name == "DeconvolutionSharpen") {
        effect = new movit::DeconvolutionSharpenEffect;
    } else if (name == "Deinterlace") {
        effect = new movit::DeinterlaceEffect;
    } else if (name == "DeinterlaceCompute") {
        effect = new movit::DeinterlaceComputeEffect;
    } else if (name == "Diffusion") {
        effect = new movit::DiffusionEffect;
    } else if (name == "OverlayMatte") {
        effect = new movit::OverlayMatteEffect;
    } else if (name == "Dither") {
        effect = new movit::DitherEffect;
    } else if (name == "FFTConvolution") {
        effect = new movit::FFTConvolutionEffect;
    } else if (name == "FFTPass") {
        effect = new movit::FFTPassEffect;
    } else if (name == "GammaCompression") {
        effect = new movit::GammaCompressionEffect;
    } else if (name == "GammaExpansion") {
        effect = new movit::GammaExpansionEffect;
    } else if (name == "Glow") {
        effect = new movit::GlowEffect;
    } else if (name == "HighlightCutoff") {
        effect = new movit::HighlightCutoffEffect;
    } else if (name == "LiftGammaGain") {
        effect = new movit::LiftGammaGainEffect;
    } else if (name == "LumaMix") {
        effect = new movit::LumaMixEffect;
    } else if (name == "Mirror") {
        effect = new movit::MirrorEffect;
    } else if (name == "Mix") {
        effect = new movit::MixEffect;
    } else if (name == "Multiply") {
        effect = new movit::MultiplyEffect;
    } else if (name == "Overlay") {
        effect = new movit::OverlayEffect;
    } else if (name == "Padding") {
        effect = new movit::PaddingEffect;
    } else if (name == "IntegralPadding") {
        effect = new movit::IntegralPaddingEffect;
    } else if (name == "IntegralPadding") {
        effect = new movit::IntegralPaddingEffect;
    } else if (name == "Resample") {
        effect = new movit::ResampleEffect;
    } else if (name == "SingleResamplePass") {
        effect = new movit::SingleResamplePassEffect;
    } else if (name == "Resize") {
        effect = new movit::ResizeEffect;
    } else if (name == "Resize") {
        effect = new movit::ResizeEffect;
    } else if (name == "Sandbox") {
        effect = new movit::SandboxEffect;
    } else if (name == "Sandbox") {
        effect = new movit::SandboxEffect;
    } else if (name == "Saturation") {
        effect = new movit::SaturationEffect;
    } else if (name == "Slice") {
        effect = new movit::SliceEffect;
    } else if (name == "UnsharpMask") {
        effect = new movit::UnsharpMaskEffect;
    } else if (name == "Vignette") {
        effect = new movit::VignetteEffect;
    } else if (name == "WhiteBalance") {
        effect = new movit::WhiteBalanceEffect;
    } else if (name == "YCbCrConversion") {
        effect = new movit::YCbCrConversionEffect;
    } else {
        std::cerr << "Unknown effect name '" << name << "'" << std::endl;
        return false;
    }

    for (const std::string &arg : stringSplit(nameAndArgs, ':')) {
        std::vector<std::string> nameAndValue = stringSplit(arg, '=');
        if (nameAndValue.size() != 2) {
            std::cout << "Invalid argument: '" << arg << "'" << std::endl;
            return false;
        }
        std::vector<std::string> stringValues = stringSplit(nameAndValue[1], ',');

        std::vector<float> values;
        for (const std::string str : stringValues) {
            float value = 0.f;
            try {
                value = std::stof(str);
            } catch (const std::invalid_argument &err) {
                std::cerr << "Invalid value " << str << std::endl;
                return false;
            }
            values.push_back(value);
        }

        const std::string &argName = nameAndValue[0];
        switch(values.size()) {
        case 1:
            effect->set_float(argName, values[0]);
            break;
        case 2:
            effect->set_vec2(argName, values.data());
            break;
        case 3:
            effect->set_vec3(argName, values.data());
            break;
        case 4:
            effect->set_vec4(argName, values.data());
            break;
        default:
            std::cerr << "Invalid number of values for argument " << str << std::endl;
            return false;
        }
    }

    d->chain->add_effect(effect.release());

    return true;
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FilterData d;
    FilterData *data;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    int err;
    std::string chainDefinition(vsapi->propGetData(in, "chain", 0, &err));
    if (err || chainDefinition.empty()) {
        std::cerr << "Invalid chain defintion: " << chainDefinition << std::endl;
        return;
    }


    d.pbufferWidth = int64ToIntS(vsapi->propGetInt(in, "contextWidth", 0, &err));
    if (err || d.pbufferWidth <= 0) {
        std::cerr << "Invalid contextWidth" << std::endl;
        return;
    }
    d.pbufferHeight = int64ToIntS(vsapi->propGetInt(in, "contextHeight", 0, &err));
    if (err || d.pbufferHeight <= 0) {
        std::cerr << "Invalid contextHeight" << std::endl;
        return;
    }

    if (!createGlContext(&d)) {
        std::cerr << "Failed to get a gl context" << std::endl;
        return;
    }

    eglMakeCurrent(d->eglDpy, d->eglSurf, d->eglSurf, d->eglCtx);

    d->chain = std::make_shared<movit::EffectChain>(d->pbufferWidth, d->pbufferHeight);

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

    if (d->vi->sampleType == stInteger) {
        switch(d->vi->bytesPerSample) {
        case 1:
            d->datatype = GL_UNSIGNED_BYTE;
            break;
        case 2:
            d->datatype = GL_UNSIGNED_SHORT;
            break;
        case 4:
            d->datatype = GL_UNSIGNED_INT_2_10_10_10_REV;
            break;
        default:
            std::cerr << "Unhandled bytes per sample" << std::cerr;
            return;
        }
    } else if (d->vi->sampleType == stFloat) {
        switch(d->vi->bytesPerSample) {
        case 2:
            d->datatype = GL_HALF_FLOAT;
            break;
        case 4:
            d->datatype = GL_FLOAT;
            break;
        default:
            std::cerr << "Unhandled bytes per sample" << std::cerr;
            return;
        }
    } else {
        std::cerr << "Unhandled sample type" << std::cerr;
        return;
    }

    switch(d->vi->colorFamily) {
    case cmRGB: {
        d->flatInput = new movit::FlatInput(inout_format, movit::FORMAT_BGRA_POSTMULTIPLIED_ALPHA, d->datatype, d->pbufferWidth, d->pbufferHeight);
        break;
    }
    case cmYUV: {
        //// todo fetch and check all the parameters
        YCbCrFormat format; /////

        if (d->vi->id == pfYUV422P8) {
            d->ycbcrInput = new movit::YCbCr422InterleavedInput(inout_format, format, d->pbufferWidth, d->pbufferHeight);
        } else {
            d->ycbcrInput = new movit::YCbCrInput(inout_format, format, d->pbufferWidth, d->pbufferHeight, movit::YCBCR_INPUT_PLANAR, d->datatype);
        }
        break;
    }

    d->chain->add_input(d->input);

    // example "Saturation:saturation=0.7 LiftGammaGain:gain=0.8,1.0,1.0"
    // don't use something similar to the ffmpeg filtergraph syntax because
    // that is braindead
    std::vector<std::string> definitions = stringSplit(chainDefinition, ' ');
    for (const std::string &definition : definitions) {
        if (!addEffect(definition, &d)) {
            return;
        }
    }
    d->chain->add_output(inout_format, movit::OUTPUT_ALPHA_FORMAT_POSTMULTIPLIED);
    d->chain->finalize();

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Movit", filterInit, filterGetFrame, filterFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("net.sesse.movit", "movit", "GPU accelerated effects and stuff", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Movit",
            "clip:clip;"
            "contextWidth:int;"
            "contextHeight:int;"
            "chain:data;",
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
            filterCreate,
            0
            plugin
    );
}
