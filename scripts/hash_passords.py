# to convert plain text passwords into sha3-512 hashes for Purgora
#
import base64
import os.path
import psycopg2
import psycopg2.extras
import subprocess
import sys
import urllib

def run_command(cmd):
    """given shell command, returns communication tuple of stdout and stderr"""
    print cmd
    return subprocess.Popen(cmd,
                            stdout=subprocess.PIPE, 
                            stderr=subprocess.PIPE, 
                            stdin=subprocess.PIPE,
                            shell=True).communicate()

def escape(str):
    return urllib.quote( unicode(str).encode('utf-8'), safe='~()*!.\'@')

def utf8_to_b64(str):
    return base64.b64encode(escape(str))

def sha3_512(str):
    cmd = 'echo -n "' + str + '" | rhash -p "%x{sha3-512}" -'
    return run_command(cmd)[0]
    


with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../pg-credentials.txt'), "r") as myfile:
    connection_string = myfile.readline().replace('\n', '')

connection = psycopg2.connect(connection_string)
cursor = connection.cursor(cursor_factory=psycopg2.extras.DictCursor)


if len(sys.argv) < 2:


    cursor.execute("select id, username, password from _ag_um_users")
    results = cursor.fetchall()

    for row in results:
        pw = row["password"]
        user = row["username"]

        print "user:", user, "pw:", pw
        print "ESC user:", escape(user), "pw:", escape(pw)
        base = utf8_to_b64(user) + (utf8_to_b64(pw))
        print "user: ", user, "base: ", base
        pw = sha3_512( base )
        hash = sha3_512(str(row["id"]) + (pw))
        print "user: ", user, "pw: ", pw
        print "user: ", user, "hash: ", hash
        print
    
        cursor.execute("UPDATE _ag_um_users SET password = %s WHERE username = %s", (hash,user))
        connection.commit()
    
#    cmd = 'echo ' + str(row["id"]) + pw + ' | rhash -p "%{sha3-512}" -'
#    hash = run_command(cmd)[0]



else:
        pw = sys.argv[2]
        user = sys.argv[1]

        cursor.execute("select id from _ag_um_users where username=%s", (user,))
        results = cursor.fetchall()
        for row in results:
            id = row["id"]

        print "user:", user, "pw:", pw
        print "ESC user:", escape(user), "pw:", escape(pw)
        base = utf8_to_b64(user) + (utf8_to_b64(pw))
        print "user: ", user, "base: ", base
        pw = sha3_512( base )
        hash = sha3_512(str(row["id"]) + (pw))
        print "user: ", user, "pw: ", pw
        print "user: ", user, "hash: ", hash
        print

        cursor.execute("UPDATE _ag_um_users SET password = %s WHERE username = %s", (hash,user))
        connection.commit()


cursor.close()
connection.close()


test = sha3_512('abc');
print "abc: ", test

test = sha3_512('');
print "empty: ", test
