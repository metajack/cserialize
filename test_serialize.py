from twisted.trial import unittest
from twisted.words.xish import domish

from cserialize import serialize

def error(expected, got):
    if type(expected) == list:
        expected = u",".join(expected)
    return "Got wrong serialization: %s (expected: %s)" % (got, expected)


class SerializeTestCase(unittest.TestCase):
    def check(self, expected, got):
        if type(expected) == list:
            self.failUnless(any([got == e for e in expected]))
        else:
            self.failUnless(expected == got, error(expected, got))

    def testSimpleElement(self):
        elem = domish.Element((None, "simple"))
        e = u"<simple/>"
        s = serialize(elem)
        self.check(e, s)

    def testSimpleWithAttrs(self):
        elem = domish.Element((None, "simple"))
        elem['to'] = 'jack'
        elem['from'] = 'kimmy'
        e = [u"<simple to='jack' from='kimmy'/>",
             u"<simple from='kimmy' to='jack'/>"]
        s = serialize(elem)
        self.check(e, s)

    def testContent(self):
        e = u"this is some content"
        s = serialize('this is some content')
        self.check(e, s)

    def testSimpleWithAttrsAndContent(self):
        elem = domish.Element((None, "simple"))
        elem['to'] = 'jack'
        elem.addContent('hello')
        e = u"<simple to='jack'>hello</simple>"
        s = serialize(elem)
        self.check(e, s)

    def testEncodedAttr(self):
        elem = domish.Element((None, 'encoded'))
        elem['a'] = 'one&two<three'
        e = u"<encoded a='one&amp;two&lt;three'/>"
        s = serialize(elem)
        self.check(e, s)

    def testEncodedContent(self):
        elem = domish.Element((None, 'encoded'))
        elem.addContent("<asdf/>")
        e = u"<encoded>&lt;asdf/&gt;</encoded>"
        s = serialize(elem)
        self.check(e, s)

    def testLongResult(self):
        elem = domish.Element((None, 'long'))
        elem.addContent("&" * 4096)
        e = u"<long>%s</long>" % ("&amp;" * 4096,)
        s = serialize(elem)
        self.check(e, s)

    def testTooManyArgs(self):
        failed = False
        try:
            serialize(1, 2, 3)
        except TypeError:
            failed = True
        except Exception, e:
            self.fail("Got bad exception: %s" % str(e))
        self.failUnless(failed, "Didn't get expected failure.")

    def testBadInput(self):
        failed = False
        try:
            serialize([])
        except TypeError:
            failed = True
        except Exception, e:
            self.fail("Got bad exception: %s" % str(e))
        self.failUnless(failed, "Didn't get expected failure.")
            
