SetHandler ag-handler
AuthType appgoo
AuthName appGoo
AuthFormPassword p_password
AuthFormUsername p_login
AuthFormLoginRequiredLocation /p/login_page
Require valid-user
<IfModule mod_headers.c>
  Header set Cache-Control "private, no-store, must-revalidate, proxy-revalidate"
</IfModule>
