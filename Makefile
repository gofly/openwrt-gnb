include $(TOPDIR)/rules.mk

PKG_NAME:=gnb
PKG_VERSION:=1.6.0a
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/gnbdev/opengnb.git
PKG_SOURCE_VERSION:=5f68b2ea0a42a2b685115043a270cdecd24f7270

PKG_LICENSE:=GPLv3
PKG_LICENSE_FILES:=COPYING
PKG_MAINTAINER:=Lixin Zheng<lixin.zhenglx@gmail.com>

PKG_USE_MIPS16:=0
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/gnb
	SECTION:=net
	CATEGORY:=Network
	TITLE:=GNB P2P Virtual Network
	URL:=https://github.com/gnbdev/opengnb
	DEPENDS:=+kmod-tun
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