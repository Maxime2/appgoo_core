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
        pgaspEnabled On
        pgaspConnectionString "host=/var/run/postgresql dbname=db_purgora user=maxime password="
        pgaspPoolKey JESLALABS
        pgaspPoolMin 2
        pgaspPoolKeep 4
        pgaspPoolMax 10
        pgaspUploadField p_file
        pgaspUploadDirectory /home/ubuntu/purgora/uploaded
        # put 0 for production
        pgaspMode 99
        # put 0 for production
        pgaspEnvironment 1
    </VirtualHost>
    <Location /path/to>
        SetHandler pgasp-handler
        rootPage login
        functionPrefix psp_
        serveFromFileSystem /assets/js, /assets/css, /assets/png
        addFunctionFormat .js js_[dir:true seperator:_]_$filename
        addFunctionFormat .css css_[dir:true seperator:_]_$filename
        addFunctionFormat "" _apache_page_request(p_request => "$dir/$filename")
        unavailableRedirectURL /errors/404.html
    </Location>
</IfModule>
```
###### <Location> ######
This is standard Apache configuration directive. Use it to speicify a location base to consider for building database function calls for. All directories apart from the exceptions listed in serveFromFileSystem will result in a database function call being generated.
You can have several Location directive as you need.

###### SetHandler ######
SetHandler is a standard Apache configuration directive with parameter *pgasp-handler* indicating specified loaction would be served by appGoo.

###### rootPage ######
This is the default page to serve if no page is specified. For example, if the user navigated to test.purgora.net, appGoo is to serve the "login" page and use a function format to build a database function call - which may actually be served from the file system dependent upon configuration.

###### functionPrefix ######
This specify prefix used to construct PostgreSQL function name based on URL requested.

###### serveFromFileSystem ######
Any qualifying directory (or directories) will attempt to have the file served from the file system rather than generating a database function call. Note that all sub-directories of the nominated directory are included in scope for being served from the file system.

###### addFunctionFormat ######
This converts the web request into a database function call. This consists of the file pattern to look for at the end of the web request (e.g ".js" would include /js/ajax.js) and the format to use to build the database call. 

There are no wildcards, and the use of "" indicates that there is NO file extension specified. If a particular file extension is not specified then it is served from the file system. For example, using the sample excerpt above if /bin/login.php is requested then it would be served from the file system as there is no qualifying file extension.

There are two primary methods for building the function call; i. Build a function call based on the web request value; ii. Have a constant function call and pass the web request in as a parameter. The latter results in the called function determining what function to call.

Within this configuration option, there are special values that can be referenced that are prefixed with a dollar symbol; $dir (the directory of the web request e.g. /assets/css) and $filename (this is the file being requested without the file extension e.g. "app" from /assets/css/app.css). All other values are constants. 

The function format can also optionally include the directory structure as part of the function call. Each directory would then be seperated by the seperator - including no seperator (""). An example would be /assets/css/app.css with a format of css_[dir:true seperator:_]_$filename would result in a function call of css_assets_css_app. Another example would be using a constant function - the same web request with a format of _apache_css_request(p_request => "$dir/$filename") would result in a function call of _apache_css_request(p_request => "/assets/css/app")

###### unavailableRedirectURL ######
If a call is made to the database but there is no valid response, then serve the user the following URL.