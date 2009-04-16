#!python.exe -O

import cgi
import cubriddb

def connectdb():
    con = cubriddb.connect('dbserver', 30000, 'blog')
    return con
    
def list(form):
    print "Content-Type: text/html\n\n"

    con = connectdb()
    c = con.cursor(1)
    c.execute('select * from posts order by id desc')
    posts = c.fetchall()

    print "<h1>PyCubrid Weblog</h1>"
    print "<div>"

    for post in posts:
            print "<h2><a href=blog.cgi?action=show&id=%s>%s</a></h2>" % (post['id'], post['title'])
            print "<p>%s</p>" % post['body']
            print "<p><small>%s</small></p>" % post['last_updated']

    print "</div>"
    print "<p><a href=blog.cgi?action=new>New Post</a></p>"
    c.close()
    con.close()

def show(form):
    print "Content-Type: text/html\n\n"

    con = connectdb()
    c = con.cursor(1)
    c.execute('select * from posts where id = ?', tuple(form['id'].value))
    posts = c.fetchall()

    print "<h1>PyCubrid Weblog</h1>"
    print "<div>"

    for post in posts: 
            print "<h2>%s</h2>" % post['title']
            print "<p>%s</p>" % post['body']
            print "<p><small>%s</small></p>" % post['last_updated']

    print "</div>"
    print "<p><a href=blog.cgi?action=edit&id=%s>Edit</a> | <a href=blog.cgi>List</a></p>" % form['id'].value
    print "<h2>Comments</h2>"

    c.execute('select * from comments where post_id = ?', tuple(form['id'].value))
    comments = c.fetchall() 
    for comment in comments: 
            print "%s" % comment['body']
            print "<hr/>"

    print "<form action='blog.cgi' method='post'>"
    print "<input type='hidden' name='action' value='comment'>"
    print "<input type='hidden' name='id' value='%s'>" % form['id'].value
    print "<textarea name='comment' cols='90' rows='5'></textarea>"
    print "<p/><input type='submit' value='comment'></form>"

    c.close()
    con.close()

edit_form = """
<h1>%s</h1>
<form action='blog.cgi' method='post'>
  <input type='hidden' name='action' value='%s'>
  <input type='hidden' name='id' value='%s'>
  <p>Title</p>
  <input type='text' name='title' value='%s' size ='90'>
  <p>Body</p>
  <textarea name='body' cols='90' rows='10'>%s</textarea>
  <p/>
  <input type='submit' value='%s'>
</form>
"""

def new(form):
    print "Content-Type: text/html\n\n"
    print edit_form % ('New Post', 'create', '', '', '', 'create')

def create(form):
    con = connectdb()
    c = con.cursor()
    c.execute('insert into posts values (ser_post_id.next_value, ?, ?, SYSTIMESTAMP)', (form['title'].value, form['body'].value))
    con.commit()
    c.close()
    con.close()

    list(form)

def edit(form):
    con = connectdb()
    c = con.cursor(1)
    c.execute('select * from posts where id = ?', tuple(form['id'].value))
    post = c.fetchone()

    print "Content-Type: text/html\n\n"
    print edit_form % ('Edit Post', 'update', post['id'], post['title'], post['body'], 'update')

    c.close()
    con.close()

def update(form):
    con = connectdb()
    c = con.cursor()
    c.execute('update posts set title = ?, body = ?, last_updated = SYSTIMESTAMP where id = ?', (form['title'].value, form['body'].value, form['id'].value))
    con.commit()
    c.close()
    con.close()

    show(form)

def comment(form):
    con = connectdb()
    c = con.cursor()
    c.execute('insert into comments values (ser_post_id.next_value, ?, SYSTIMESTAMP, ?)', (form['comment'].value, form['id'].value))
    con.commit()
    c.close()
    con.close()

    show(form)

action_map = {"new":new, "create":create, "list":list, "show":show, "edit":edit, "update":update, "comment":comment}

form = cgi.FieldStorage()
if form.has_key('action'):
        action = action_map[form['action'].value];
else:
        action = list;

action(form)
