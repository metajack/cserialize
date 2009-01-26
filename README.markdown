# cserialize

Twisted Python comes with an XMPP capable XML parser called Domish.
Domish has a highly optimized, pure Python XML serializer, but even as
optimized as it is, it is still quite slow.

cserialize is a Domish serializer written in C, and is approximately 4
times as fast as the pure Python one.

## License

This code is copyright (c) 2008 by Jack Moffitt <jack@metajack.im> and
is available under the [MIT
license](http://www.opensource.org/licenses/mit-license.php).  See
`LICENSE.txt` for details.

## Dependencies

* [Python](http://www.python.org) 2.4 or later
* [Twisted](http://www.twistedmatrix.com) 8.1.x or later

