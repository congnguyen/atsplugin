# using tool tsxs in the bin directory

# BUILD plugin
    sudo ./tsxs -o add_text.so -c ~/add_text.cc

# INSTALL plugin
    sudo ./tsxs -o add_text.so -i

# CONFIGURE plugin (/opt/ts//etc/trafficserver/plugin.config)
    add_text.so Created with NetCDN Origin Packager

# CONFIGURE test with url m3u8 (/opt/ts//etc/trafficserver/remap.config)
     map http://172.16.201.147:8080/ http://playertest.longtailvideo.com/adaptive/wowzaid3/playlist.m3u8
# CONFIGURE (/opt/ts//etc/trafficserver/records.config)
    CONFIG proxy.config.url_remap.remap_required INT 1

