include $(TOPDIR)/rules.mk

PKG_NAME:=gnb
PKG_VERSION:=master
PKG_RELEASE:=1

PKG_LICENSE:=GPLv3
PKG_LICENSE_FILES:=COPYING
PKG_MAINTAINER:=Lixin Zheng<lixin.zhenglx@gmail.com>

PKG_BUILD_DEPENDS:=zlib

include $(INCLUDE_DIR)/package.mk

define Package/gnb
	SECTION:=net
	CATEGORY:=Network
	TITLE:=GNB P2P Virtual Network
	URL:=https://github.com/gnbdev/opengnb
	DEPENDS:=kmod-tun libmbedtls zlib
endef

define Package/gnb/description
	GNB is open source de-centralized SDVN to achieve layer3 network via p2p with the ultimate capability of NAT Traversal.
endef

define Build/Compile
	(cd $(PKG_BUILD_DIR); cp -f Makefile.openwrt Makefile)
	$(call Build/Compile/Default)
endef

define Package/gnb/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/gnb $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/gnb_crypto $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/gnb_ctl $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/gnb_es $(1)/usr/bin/
endef

$(eval $(call BuildPackage,gnb))
