#!/bin/sh

cd $1

[ -e common.mk ] || exit 0

patch -p1 <<EOF
diff --git a/common.mk b/common.mk
--- a/common.mk
+++ b/common.mk
@@ -175,6 +175,7 @@ warn_signed_modules += \\
     echo "*** disabled for this build." ;
 endif # CONFIG_MODULE_SIG_ALL=y
 ifeq (\${CONFIG_MODULE_SIG_FORCE},1)
+warn_signed_modules += \\
     echo "warning: The target kernel has CONFIG_MODULE_SIG_FORCE enabled," ; \\
     echo "warning: but the signing key cannot be found. The module must" ; \\
     echo "warning: be signed manually using 'scripts/sign-file'." ;
EOF

if ! [ -e "/lib/modules/6.11.0-13-generic/build/include/linux/auxiliary_bus.h" ]
then
	sed -i -e 's|^#include <linux/auxiliary_bus\.h>|#include "linux/auxiliary_bus.h"|' *_client.h || true
fi
