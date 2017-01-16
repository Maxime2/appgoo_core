# Common configuration directive for an appGoo web-site
#
ServerAlias +local.appgoo.net
DocumentRoot /home/ubuntu/appgoo_core
#LogLevel debug
DirectoryIndex index.html
AddDefaultCharset UTF-8
ServerSignature Off
ErrorDocument 401 "/p/login_page"

# KEPT_BODY filter to store request body for subrequests
SetInputFilter KEPT_BODY

<Location /syslog>
        SetHandler ag-syslog
	AuthType appgoo
	AuthName appGoo
        AuthFormLoginRequiredLocation /p/login_page
	Require valid-user
        Include sites-available/ag-sessions.inc
</Location>

<LocationMatch "^/p/.*">
        Include sites-available/ag-location.inc
        Include sites-available/ag-sessions.inc
</LocationMatch>

<LocationMatch "^/json/.*">
        agContentType application/json
        Include sites-available/ag-location.inc
        Include sites-available/ag-sessions.inc
</LocationMatch>

<LocationMatch "^/js/.*">
        agContentType application/javascript
        Include sites-available/ag-location.inc
        Include sites-available/ag-sessions.inc
</LocationMatch>

<LocationMatch "^/css/.*">
        agContentType text/css
        Include sites-available/ag-location.inc
        Include sites-available/ag-sessions.inc
</LocationMatch>


<IfModule mod_expires.c>
ExpiresActive On
ExpiresDefault "access plus 1 second"
ExpiresByType application/font-sfnt "access plus 1 hour"
ExpiresByType application/font-woff "access plus 1 hour"
ExpiresByType image/png "access plus 1 hour"
ExpiresByType application/javascript "access plus 1 hour"
ExpiresByType text/css "access plus 1 hour"
</IfModule>

<Location /server-status>
#	SetHandler server-status
#	Require all granted
       Require all denied
</Location>
#
<Location /server-info>
#	SetHandler server-info
#	Require all granted
       Require all denied
</Location>

<LocationMatch "/(p|json)/(add_assessment_document_page|add_task_document_page|upload_file)">
       SetInputFilter tmpfile-filter;upload-filter
</LocationMatch>


<Directory /home/ubuntu/appgoo_core>
	Require all granted
#	Order allow,deny
#	allow from all
	Options -Indexes +Includes +FollowSymLinks
        SetOutputFilter INCLUDES;DEFLATE
</Directory>

<Location /p/login_page>
	AuthType None
	Require all granted
</Location>

<Location /p/dologin.html>
	SetHandler appgoo-login-handler
	AuthFormLoginSuccessLocation /
</Location>

<Location /p/dologout.html>
	SetHandler appgoo-logout-handler
	AuthFormLogoutLocation /p/login_page
</Location>

<Directory /home/ubuntu/appgoo_core/assets>
        Satisfy Any
</Directory>
<Directory /home/ubuntu/appgoo_core/attachments>
        Satisfy Any
</Directory>

<IfModule mod_headers.c>

	Header set Content-Security-Policy "default-src 'self' 'unsafe-inline'; img-src 'self' data:; script-src 'self' 'unsafe-inline' 'unsafe-eval' ajax.googleapis.com; style-src 'self' 'unsafe-inline' maxcdn.bootstrapcdn.com; report-uri /syslog/error; child-src data:; font-src 'self' 'unsafe-inline' data:"

    # `mod_headers` cannot match based on the content-type, however,
    # the `Content-Security-Policy` response header should be send
    # only for HTML documents and not for the other resources.

    <FilesMatch "\.(appcache|atom|bbaw|bmp|crx|css|cur|eot|f4[abpv]|flv|geojson|gif|htc|ico|jpe?g|js|json(ld)?|m4[av]|manifest|map|mp4|oex|og[agv]|opus|otf|pdf|png|rdf|rss|safariextz|svgz?|swf|topojson|tt[cf]|txt|vcard|vcf|vtt|webapp|web[mp]|webmanifest|woff2?|xloc|xml|xpi)$">
        Header unset Content-Security-Policy
    </FilesMatch>

</IfModule>


<IfModule pagespeed_module>

ModPagespeedRewriteLevel OptimizeForBandwidth
ModPagespeedEnableFilters collapse_whitespace,combine_javascript,local_storage_cache,inline_preview_images,insert_dns_prefetch
ModPagespeedForbidFilters rewrite_css

ModPagespeedAvoidRenamingIntrospectiveJavascript off
ModPagespeedImplicitCacheTtlMs 10
ModPagespeedInPlaceResourceOptimization on
ModPagespeedInPlaceRewriteDeadlineMs 200

ModPagespeedFetchHttps enable
ModPagespeedSslCertDirectory /etc/ssl/certs
ModPagespeedSslCertFile /etc/ssl/private/purgora-net.key

#ModPagespeedAllow "*.psp"
#ModPagespeedDisallow https://local.appgoo.net/*

ModPagespeedFetchWithGzip on
SetOutputFilter DEFLATE

    <Location /pagespeed_admin>
        Order allow,deny
        Allow from localhost
        Allow from 127.0.0.1
        Allow from 192.168.0
        SetHandler pagespeed_admin
    </Location>

</IfModule>
