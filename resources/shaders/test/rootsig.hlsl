#define ROOTSIG_EMPTY "RootFlags(0)"
#define ROOTSIG_SRV "RootFlags(DENY_VERTEX_SHADER_ROOT_ACCESS),"  \
  "DescriptorTable(SRV(t0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE))," \
  "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP,visibility=SHADER_VISIBILITY_PIXEL)"
