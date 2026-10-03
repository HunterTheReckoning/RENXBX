Xbox (nxdk) build only: stand-ins for old Microsoft headers that nxdk does not provide.
Put this folder on the include path for nxdk builds only (-I Code/xbox/include), never for
the PC build, so it cannot shadow the real MSVC headers.
