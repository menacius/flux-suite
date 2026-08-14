#include "flux-encoder-provider-abi.h"
#include "providers/render-source-descriptor.h"
#include <QCoreApplication>
#include <cassert>
int main(int argc,char **argv){QCoreApplication app(argc,argv);static_assert(FLUX_ENCODER_RENDER_PROVIDER_ABI_VERSION==1u);FluxEncoderProviderInfo info{};info.struct_size=sizeof(info);info.abi_version=FLUX_ENCODER_RENDER_PROVIDER_ABI_VERSION;assert(info.struct_size>=sizeof(uint32_t)*2);const auto descriptor=flux::RenderSourceDescriptor::forProject(QStringLiteral("org.fluxmotion.renderer"),QStringLiteral("test.fxmt"));const auto json=descriptor.toJson();assert(json.value("schemaVersion").toInt()==1);assert(json.value("providerId").toString()==QStringLiteral("org.fluxmotion.renderer"));return 0;}
