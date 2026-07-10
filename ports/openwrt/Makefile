# OpenWrt 软件包(feed 用法：把本目录加入 feeds 或复制到 package/omnisms)
# 状态：标准 CMake 包写法，尚未在真实 OpenWrt buildroot 上验证——首次使用请
# 按实际 SDK 版本核对依赖名(libcurl4/libcurl)。
include $(TOPDIR)/rules.mk

PKG_NAME:=omnisms
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/MineSunshineone/OmniSMS.git
PKG_SOURCE_VERSION:=master

PKG_BUILD_PARALLEL:=1
CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_OPTIONS += -DOMNISMS_BUILD_TESTS=OFF -DOMNISMS_BUILD_LINUX_PORT=ON

define Package/omnisms
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OmniSMS forwarding daemon
  DEPENDS:=+libcurl +libstdcpp +libpthread
endef

define Package/omnisms/description
  Cross-platform SMS forwarding daemon (serial AT modem -> push channels).
endef

define Package/omnisms/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/omnismsd $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/omnisms
	$(INSTALL_CONF) ./files/omnisms.conf $(1)/etc/omnisms/omnisms.conf
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/omnisms.init $(1)/etc/init.d/omnisms
endef

$(eval $(call BuildPackage,omnisms))
