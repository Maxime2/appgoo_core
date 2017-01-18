<IfModule mod_ag.c>
agEnabled On
agConnectionString "host=/var/run/postgresql dbname=appgoo user=ubuntu password=appgoo"
#AddHandler ag-handler .ag
#AddType text/html .ag
agPoolKey APPGOO
agPoolMin 2
agPoolKeep 4
agPoolMax 10
agUploadField p_file
agUploadDirectory /home/ubuntu/purgora/uploaded
</IfModule>
