kjson
=====
A [JSON](http://json.org/) parser in C11, which assumes that the input string is writeable.
It focusses on speed and portability and it provides 3 layers of API: low, mid and high.
The first two layers use constant memory, while the third one requires dynamic memory linear
in the number of array or object entries. The second layer 'mid' is a callback-based parser
API with two versions, one of which requires constant memory independent of the depth of the
parse tree. In the implementation, each layer builds upon the previous.

Special care has been taken to speed up string processing as it is the most common token type
in JSON documents: each member of an object has a key coded as a string and values also can
take strings, as can array elements.
JSON has some weird idea about strings:
The source document itself is to be encoded in UTF-8, while string tokens allow escape
sequences of the form `\uXXXX` denoting **not** a *code point*, but rather a *code unit*.
Code units are the elements UTF-16 encodes to: 16-bit integers also with some escape sequences
called surrogate pairs. These are 2 such integers each of specific range which in sequence
form a code unit outside the Basic Multilingual Plane (such as e.g. Emojis and many others).
For decoding into UTF-8 (which is the defacto standard nowadays and especially suited
for the string functions from the C standard library) this means a round-trip via UTF-16 in
order to decode JSON strings appropriately. In addition, JSON does not require an
`\u`-escape denoting the first element of a surrogate pair to be followed by the second
surrogate unit (and does not even specify any conditions for them). We handle this condition
gracefully.
