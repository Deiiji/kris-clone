# -*- cmake -*-

# set(STROPHE_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
# set(STROPHE_LIBRARIES strophe)

include(Prebuilt)

set(Strophe_FIND_QUIETLY ON)
set(Strophe_FIND_REQUIRED ON)

if (STANDALONE)
  include(FindStrophe)
else (STANDALONE)
  use_prebuilt_binary(strophe)
  if (WINDOWS)
    set(STROPHE_LIBRARIES strophe)
  endif (WINDOWS)
  if (DARWIN)
    set(STROPHE_LIBRARIES strophe llcrypto llssl)
  endif (DARWIN)
  if (LINUX)
    set(STROPHE_LIBRARIES strophe crypto ssl)
  endif (LINUX)
  set(STROPHE_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (STANDALONE)

# if (LINUX)
#   set(CRYPTO_LIBRARIES crypto)
# elseif (DARWIN)
#   set(CRYPTO_LIBRARIES llcrypto llssl)
# endif (LINUX)
