bin_PROGRAMS = vdcpass

vdcpass_SOURCES = \
    main.c \
    parseargs.c

vdcpass_CPPFLAGS = \
    -I$(top_srcdir)/vmdir/include \
    -I$(top_srcdir)/vmdir/include/public \
    -I$(top_srcdir)/vmdir/tools/include \
    @LW_INCLUDES@ \
    @OPENSSL_INCLUDES@

vdcpass_LDADD = \
    $(top_builddir)/vmdir/client/libvmdirclient.la \
    $(top_builddir)/vmdir/common/libcommon.la \
    @DCERPC_LIBS@ \
    @LWIO_LIBS@ \
    @SCHANNEL_LIBS@ \
    @LWMSG_LIBS@ \
    @LWRSUTILS_LIBS@ \
    @LWREG_LIBS@ \
    @LWBASE_LIBS@ \
    @CRYPTO_LIBS@ \
    @CRYPT_LIBS@ \
    @GSSAPI_LIBS@ \
    @UUID_LIBS@ \
    @SASL_LIBS@ \
    @LDAP_LIBS@ \
    @LBER_LIBS@ \
    @DL_LIBS@

vdcpass_LDFLAGS = \
    @OPENSSL_LDFLAGS@ \
    @LW_LDFLAGS@

