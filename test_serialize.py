from twisted.trial import unittest
from twisted.words.xish import domish

from cserialize import serialize

def error(expected, got):
    if type(expected) == list:
        expected = u",".join(expected)
    return u"Got wrong serialization: %s (expected: %s)" % (got, expected)


class SerializeTestCase(unittest.TestCase):
    def check(self, expected, got):
        if type(expected) == list:
            self.failUnless(any([got == e for e in expected]), 
                            error(expected, got))
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
        elem['a'] = 'one&two<three\'four'
        e = u"<encoded a='one&amp;two&lt;three&apos;four'/>"
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
            
    def testCloseElement(self):
        elem = domish.Element((None, 'unclosed'))
        e = u"<unclosed>"
        s = serialize(elem, closeElement=0)
        self.check(e, s)

    def testDefaultNamespace(self):
        elem = domish.Element(('somens', 'element'))
        e = u"<element xmlns='somens'/>"
        s = serialize(elem)
        self.check(e, s)

    def testChildDefaultNamespace(self):
        elem = domish.Element(('somens', 'element'))
        elem.addElement('child')
        e = u"<element xmlns='somens'><child/></element>"
        s = serialize(elem)
        self.check(e, s)

    def testChildSameNamespace(self):
        elem = domish.Element(('somens', 'element'))
        elem.addElement(('somens', 'child'))
        e = u"<element xmlns='somens'><child/></element>"
        s = serialize(elem)
        self.check(e, s)

    def testChildOtherDefaultNamespace(self):
        elem = domish.Element(('ns1', 'parent'))
        elem.addElement(('ns2', 'child'))
        e = u"<parent xmlns='ns1'><child xmlns='ns2'/></parent>"
        s = serialize(elem)
        self.check(e, s)

    def testOnlyChildDefaultNamespace(self):
        elem = domish.Element((None, 'parent'))
        elem.addElement(('somens', 'child'))
        e = u"<parent><child xmlns='somens'/></parent>"
        s = serialize(elem)
        self.check(e, s)

    def testOtherNamespace(self):
        elem = domish.Element(('ns1', 'foo'), 'ns2')
        e = [u"<prefix:foo xmlns:prefix='ns1' xmlns='ns2'/>",
             u"<prefix:foo xmlns='ns2' xmlns:prefix='ns1'/>"]
        s = serialize(elem, prefixes={'ns1': 'prefix'})
        self.check(e, s)

    def testOtherNamespaceWithChild(self):
        elem = domish.Element(('ns1', 'parent'), 'ns2')
        elem.addElement(('ns1', 'child'), 'ns2')
        e = [u"<prefix:parent xmlns:prefix='ns1' xmlns='ns2'><prefix:child/>"\
                 "</prefix:parent>",
             u"<prefix:parent xmlns='ns2' xmlns:prefix='ns1'><prefix:child/>"\
                 "</prefix:parent>"]
        s = serialize(elem, prefixes={'ns1': 'prefix'})
        self.check(e, s)

    def testChildInDefaultNamespace(self):
        elem = domish.Element(('ns1', 'parent'), 'ns2')
        elem.addElement(('ns2', 'child'))
        e = u"<prefix:parent xmlns='ns2' xmlns:prefix='ns1'><child/>"\
            "</prefix:parent>"
        s = serialize(elem, prefixes={'ns1': 'prefix'})
        self.check(e, s)

    def testQualifiedAttribute(self):
        elem = domish.Element((None, 'foo'))
        elem[('somens', 'bar')] = 'baz'
        e = u"<foo xn0:bar='baz' xmlns:xn0='somens'/>"
        s = serialize(elem)
        self.check(e, s)

    def testQualifiedAttributeDefaultNS(self):
        elem = domish.Element(('somens', 'foo'))
        elem[('somens', 'bar')] = 'baz'
        e = u"<foo xn0:bar='baz' xmlns='somens' xmlns:xn0='somens'/>"
        s = serialize(elem)
        self.check(e, s)
        
    def testTwoChildren(self):
        elem = domish.Element((None, 'foo'))
        child1 = elem.addElement(('ns1', 'bar'), 'ns2')
        child1.addElement(('ns2', 'quux'))
        child2 = elem.addElement(('ns3', 'baz'), 'ns4')
        child2.addElement(('ns1', 'quux'))
        e = u"<foo><xn0:bar xmlns='ns2' xmlns:xn0='ns1'><quux/></xn0:bar>"\
            "<xn1:baz xmlns='ns4' xmlns:xn1='ns3'><xn0:quux xmlns:xn0='ns1'/>"\
            "</xn1:baz></foo>"
        s = serialize(elem)
        self.check(e, s)

    def testXMLNamespace(self):
        elem = domish.Element((None, 'foo'))
        elem[('http://www.w3.org/XML/1998/namespace', 'lang')] = 'en_US'
        e = u"<foo xml:lang='en_US'/>"
        s = serialize(elem)
        self.check(e, s)

    def testLocalPrefixes(self):
        elem = domish.Element(('somens', 'foo'),
                              localPrefixes={'somens': 'bar'})
        e = u"<bar:foo xmlns:bar='somens'/>"
        s = serialize(elem)
        self.check(e, s)

    def testLocalPrefixesWithChild(self):
        elem = domish.Element(('somens', 'foo'),
                              localPrefixes={'somens': 'bar'})
        elem.addElement('baz')
        e = u"<bar:foo xmlns:bar='somens'><baz/></bar:foo>"
        s = serialize(elem)
        self.check(e, s)

    def testRawXMLSerialization(self):
        elem = domish.Element((None, "foo"))
        elem.addRawXml("<abc123>")
        e = u"<foo><abc123></foo>"
        s = serialize(elem)
        self.check(e, s)

    def testRawXMLWithUnicodeSerialization(self):
        elem = domish.Element((None, 'foo'))
        elem.addRawXml(u"<degree>\u0080</degree>")
        e = u"<foo><degree>\u0080</degree></foo>"
        s = serialize(elem)
        self.check(e, s)
        

