## About appGoo Core ##

This describes the use and requirements for the core components of appGoo

### What is appGoo Core ? ###

appGoo Core is an Apache module that intercepts web requests to s specific location and prepares a direct call to a PostgreSQL database. Whilst not addressed by the core, PostgreSQL will dynamically build the content required and return it to Apache to send it to the origin.

### Uses ###
Any web page that would typically be served from a CMS or written in client JS, CSS & HTML5.

### Key Attributes ###

* Fast. The interpretation of the web request to a call to the database is performed extremely quickly
* Configurable. How web requests are recognised as requiring database calls as opposed to traditional file system operations is configured per instance. The database call itself is dynamically generated and the format of this call is also configurable
* Invisible to the end-user. There is no difference to the user interacting with a web page generated via appGoo as opposed to any other web page

### Configuration ###
####   Configuration File ####
The apache httpd.conf file.

####   Configuration Options ####
Below is an excerpt sample from httpd.conf

```
#!shell

<IfModule appgoo_mod>
    <VirtualHost *:80>
        agEnabled On
        agConnStr "host=/var/run/postgresql dbname=example user=appgoo password="
        agPoolKey APPGOO
        agPoolMin 10
        agPoolKeep 50
        agPoolMax 90
        pgaspUploadField p_file
        pgaspUploadDirectory /home/ubuntu/purgora/uploaded
        # put 0 for production
        pgaspMode 99
        # put 0 for production
        pgaspEnvironment 1
        <Location /path/to>
            SetHandler pgasp-handler
            agRootPage login
            functionPrefix psp_
            agServeFiles /assets/js, /assets/css, /assets/png
            agAddFuncFormat .js js_[dir:true seperator:_]_$filename
            agAddFuncFormat .css css_[dir:true seperator:_]_$filename
            agAddFuncFormat "" _apache_page_request(p_request => "$dir/$filename")
            agUnavailLimit 10
            agUnavailURL /errors/404.html
        </Location>
        <LocationMatch "/path/to/(add_document_page|upload_file)">
            SetInputFilter tmpfile-filter;upload-filter
        </LocationMatch>
    </VirtualHost>
</IfModule>
```
###### `<VirtualHost>` ######
You can specify here which virtual host would be served with appGoo. You may omit VirtualHost directive, in that case appGoo will serve any virtual host. You may also have different appGoo configurations for different virtual hosts.

###### agEnabled ######
_default: On._ If not enabled (Off), then appGoo does not do any processing and control is returned to Apache to complete.

###### agConnStr ######
This is the connection string to the database - it references the full path and other details but should not include the database password

###### agPoolKey ######
_default: APPGOO._ This key is hashed and is used by Apache to determine the connection pool to use for connecting to the database

###### agPoolMin ######
_default: 10._ The minimum number of connections to be maintained to the database. When Apache starts, this number of connections will be created to the database

###### agPoolKeep ######
_default: 50._ As the number of connections concurrently required increase from the minimum connections specified, the pool will maintain this number of connections available until this setting is met. Any connections required greater than this setting will result in a new connection being made and then terminated once completed which is a more expensive operation than utilising existing available connections. This setting should be equal to or greater than the minimum number of connections and should be equal or less than the maximum number of connections specified. PostgreSQL consumes approximately 500 bytes of memory + logfile space for each open connection maintained. The number of connections that you would wish to remain open should be your maximum number of users * the concurrency factor (what percentage of those users will be performing work at the same time) + any concurrent API requests that are expected to occur. For example, if there were 50 application users with a 60% concurrency and 2 concurrent API requests, then the 'keep' setting should be (50*0.6)+2 = 32. This would occupy approximately 32*500 bytes (16k) of server memory + logfile space

###### agPoolMax ######
_default: 90._ This is the maximum number of database connections to maintain as available - it has to be greater or equal to both the minimum number of connections and the keep number of connections. This should be set at 2-5 connections less than the PostgreSQL 'maxConnections' setting. The default PostgreSQL value is 100 connections, in this case the value for this setting should be 95-98. As new concurrent connections are required they will be dynamically created and destroyed when the request is complete. This setting determines the maximum number of concurrent connections that are permitted to be created by the appGoo connection pool. Any excess requests will wait until a connection slot becomes available

###### agRootPage ######
This is the page to serve to the user if no page is specified - in other words, the root domain is specified like https://example.com. Typically this would be a login page or an information page

###### agServeFiles ######
_default: /assets/._ Any file that is found in a qualifying directory - including sub-directories, will be served from the filesystem rather than transformed into a database function call. Therefore, you could have files such as 'app.css' and 'app.js' served from the /assets directory whilst other CSS and JS files retrieved from the database

###### agAddFuncFormat ######
_default: "" appgoo/_$filename._ Add as many formats as required for each type of file specified via web requests to transform the request into a database function to retrieve from the database. By default, only page requests will be transformed into database function calls, all other request types (e.g .js, .css, .png, .woff, etc...) would be served from the filesystem regardless of the directory specified and the setting of 'agServeFiles'. If you wish to serve another file type from the database, add an additional 'agAddFuncFormat'. appGoo will build a database function call based upon the format provided here and can include both the filename and the directory structure specified as part of the function name.

There are no wildcards, and the use of "" indicates that there is NO file extension specified. If a particular file extension is not specified then it is served from the file system. For example, using the sample excerpt above if /bin/login.php is requested then it would be served from the file system as there is no qualifying file extension.

There are two primary methods for building the function call; i. Build a function call based on the web request value; ii. Have a constant function call and pass the web request in as a parameter. The latter results in the called function determining what function to call.

Within this configuration option, there are special values that can be referenced that are prefixed with a dollar symbol; $dir (the directory of the web request e.g. /assets/css) and $filename (this is the file being requested without the file extension e.g. "app" from /assets/css/app.css). All other values are constants. 

The function format can also optionally include the directory structure as part of the function call. Each directory would then be seperated by the seperator - including no seperator (""). An example would be /assets/css/app.css with a format of css_[dir:true seperator:_]_$filename would result in a function call of css_assets_css_app. Another example would be using a constant function - the same web request with a format of _apache_css_request(p_request => "$dir/$filename") would result in a function call of _apache_css_request(p_request => "/assets/css/app")


###### agUnavailLimit ######
_default: 10._ If a timeout occurs from it either being unavailable or performing extremely poorly, then this is the number of seconds to wait before returning the 'agUnavailURL' to the user.

###### agUnavailURL ######
_default: ""._ This is the URL to return to the user if the 'agUnavailLimit' is breached. Typically, this should be a standalone html page in the file system. Note that this can only return a file from the filesystem, not the database



## Config Items to be finalised ##

###### pgaspUploadFiled ######
This directive specify the name of form field holding filename to save uploading data into.

###### pgaspUploadDirectory ######
This directive specify the directory where all uploaded files are stored. This directory must be writable for the user under which Apache server is running.

###### pgaspMode ######
A non-zero value set by this directive enables output about any error occuring into the output page (assuming it is an HTML page).

###### pgaspEnvironment ######
A non-zero value set by this directive enables tracing of execution time for each pgasp function called in progress ti build response page (assuming it is an HTML page).

###### `<Location>` ######
This is standard Apache configuration directive. Use it to speicify a location base to consider for building database function calls for. All directories apart from the exceptions listed in serveFromFileSystem will result in a database function call being generated.
You can have several Location directive as you need.

###### SetHandler ######
SetHandler is a standard Apache configuration directive with parameter *pgasp-handler* indicating specified loaction would be served by appGoo.

###### functionPrefix ######
This specify prefix used to construct PostgreSQL function name based on URL requested.

###### SetInputFilter tmpfile-filter;upload-filter ######
This directive enables multi-part upload form processing for any location specified.