## About appGoo Core ##

This describes the use and requirements for the core components of appGoo

### What is appGoo Core ? ###

appGoo Core is an Apache mod that intercepts web requests and prepares a direct call to a PostgreSQL database. Whilst not addressed by the core, PostgreSQL will dynamically build the content required and return it to Apache to send it to the origin.

### Key Attributes ###

* Invisible to the end-user. There is no difference to the user interacting with a web page generated via appGoo as opposed to any other web page
* Fast. The interpretation of the web request to a call to the database is performed extremely quickly
* Configurable. How web requests are recognised as requiring database calls as opposed to traditional file system operations is configured per instance. The database call itself is dynamically generated and the format of this call is also configurable

### Configuration ###
####   Configuration File ####
???

####   Configuration Options ####
Below is an excerpt sample from httpd.conf

```
#!shell

<IfModule appgoo_mod>
    domainAddress test.purgora.net
    serveFromFileSystem /assets/js, /assets/css, /assets/png
    addFunctionFormat "" pg_[dir:true seperator:_]_$filename
    addFunctionFormat .js js_[dir:true seperator:_]_$filename
    addFunctionFormat .css css_[dir:true seperator:_]_$filename
    addFunctionFormat "" _apache_page_request(p_request => $dir/$filename)
    unavailableRedirectURL /errors/404.html    
</IfModule>
```


* Summary of set up
* Configuration
* Dependencies
* Database configuration
* How to run tests
* Deployment instructions

### Contribution guidelines ###

* Writing tests
* Code review
* Other guidelines

### Who do I talk to? ###

* Repo owner or admin
* Other community or team contact