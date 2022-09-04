# Serialization and deserialization

## Contents

This document covers the facilities provided by the data container for selectively turning its contents into an array of bytes (serialization) and for selectively reading an array of bytes to return the state of the data container, in whole or in part, to what it was when those bytes were created by serialization (deserialization). While the facilities provided are selective in the sense that you can choose to serialize only some types of object and only some of their properties, they are not currently selective "horizontally," meaning that if you serialize a property, you will be serializing the values stored for all object instances of a particular type.

## Core concepts

### Tags

Selecting what gets serialized is managed by the `tag{...}` keys that can be placed in any object definition, relationship definition, or property definition. The contents of the `tag` parameter can be an arbitrary string. Case matters when it comes to matching one tag to another, and while initial and trailing whitespace will be ignored, any whitespace in the middle of the tag will also matter when it comes to matching tags.

#### Using tags in a `load_save` definition

Deciding what to serialize or deserialize is determined by a combination of selecting tags that will determine which object and relationships are included or excluded and selecting tags that determine which of the properties found in those objects and relationships will be included or excluded. In more detail: First you must decide whether you want to filter the objects and relationships by either including all those that have a tag belonging to a set of tags you provide or by excluding those that have a tag belonging to that set. Then you must make the same choice for properties, filtering them either by including all of the properties that include a tag in the set or excluding all of the properties that include a tag in the set. When executing a serialize operation only the properties that were not filtered out by the tags you have chosen that belong to a type of object that was not filtered out will be serialized.

In addition to serializing the contents of properties, any object type that is not filtered out (regardless of whether any of its properties met the selection criteria) will serialize the number of current object instances (and for those stored as `erasable`, which slots are empty) so that upon deserialization the same overall state of which object instances exist can be restored. The same information is stored for relationships, but in addition when the relationship type is not filtered out, all of the links stored in the relationship instances will be serialized. Note that, unlike properties, it is not possible to selectively serialize just some of the links in a relationship; it is all or nothing.

To communicate the decisions you have made about serialization to the generator, you need to include one or more `load_save{...}` keys at the top level of the file. Each `load_save` key must have a single parameter that will in turn be parsed as a sequence of sub-keys. Each `load_save` key most contain a `name{...}` sub-key, which has as its parameter a valid C++ identifier. Then the `load_save` key may have either a `only_objects` or `exclude_objects` sub-key with one or more parameters. For example if `only_objects{tag A}{tag B}` was present, this would tell the generator that you only want to serialize objects with either the `tag A` tag or the `tag B` tag. If you include neither `only_objects` or `exclude_objects` as a sub-key, then every object will pass the filter and be a candidate for serialization. And in the same way the `load_save` key may contain either `only_properties` or `exclude_properties` followed by a number of parameters identifying the tags to filter by, which work in the same way except for properties.

### Loading records

For each `load_save` definition that is present in the file, the generator will produce a `make_serialize_record_𝘯𝘢𝘮𝘦()` function, where the name is determined by the sub-key in the `load_save` key. This function returns a `load_record` structure that encodes which objects, relationships, and properties that are selected by the `load_save` definition. This structure consists internally of one packed `bool` member for each object, relationship, and property, and you may manually set or unset any of them to control what exactly is serialized or deserialized.

Each data container also comes with a `serialize_entire_container_record()` which returns a `load_record` with every member set to `true`. This can be used if you simply want to serialize everything and don't want to bother writing a `load_save` definition.

## Serialization

Serialization is done in two phases. First you must call the data container's `serialize_size(load_record const&)` function which returns the number of bytes that will be required to store the information requested by the `load_record`. Then, after allocating or otherwise reserving the required memory, you may invoke the data container's `serialize(std::byte*&, load_record const&)`
function. The first parameter is a *reference* to a `std::byte` pointer to the beginning of the allocated memory, and the second must be a `load_record` identical to the one that was passed to `serialize_size`. After the function returns, the `std::byte` pointer will point to the `std::byte` following the last byte written by the function. Normally this will be useless, as the `serialize` function will write exactly as many bytes as `serialize_size` requested, but it is possible for a user-defined function (see [Serializing and deserializing complex types](#serializing-and-deserializing-complex-types) below) may request more space than it actually uses.

## Deserialization

There are two deserialization functions provided by the data container:
`deserialize(std::byte const*&, std::byte const*, load_record&)` and `deserialize(std::byte const*&, std::byte const*, load_record&, load_record const&)`. The first parameter to both functions is a *reference* to a `std::byte` pointer to the first byte of memory to be deserialized from. The second parameter is a `std::byte` pointer to the first byte after the valid data to deserialize from. Not only does this tell the function where to stop, it is also used to ensure that corrupted or erroneous data doesn't result in nonsensical reads. The third parameter is a load record that serves as an output parameter. A new `load_record` should be passed in (i.e. with all its members set to `false`), and upon returning this `load_record` will have its members set to true if the deserialization routine found data for the corresponding object, relationship, or property in the bytes passed to it. If you are loading data from the serialization of an older definition that did not have some of the properties in the current version, you can detect those missing properties by checking the appropriate member and then, upon detecting that it is set to `false` when you expected the data to be found, generate or otherwise intelligently react to the missing data. Finally, the second deserialization function takes an extra fourth parameter in the form of a second `load_record`. This `load_record` acts as a "mask," and only objects and properties with their corresponding member in the record set to `true` will be deserialized, even if data is present for them. This may be useful to you if you mind that you want to reset just part of the data container to the state contained in the serialized bytes or if you have subsequently split up into distinct serialization destinations objects and properties that used to be mixed together, and want to be able to load this old data for one of the new parts without affecting the other.

One wrinkle to be aware of when deserializing data is how relationships are affected. If you deserialize a type of object and there are fewer objects of that type after deserialization than there were before (meaning that some handles to previously valid object instances may no longer be valid), then all relationships involving those objects will be reset (i.e. all relationship instances will be deleted). If you have serialized the objects and their involved relationships together, then they will automatically be ordered so that the relationships will be loaded after the objects, and thus there will not be a problem. However, if you serialize them separately, then you will have to take care to deserialize the objects first.

Note that while deserialization overwrites existing data, it will not clear data that is already present if no data has been serialized for those properties. To ensure that old data doesn't stick around you may either call the data container's `reset()` function before deserializing, which will clear all data currently stored, or you can call `𝘰𝘣𝘫𝘦𝘤𝘵/𝘳𝘦𝘭𝘢𝘵𝘪𝘰𝘯𝘴𝘩𝘪𝘱 𝘯𝘢𝘮𝘦_resize(0)` which will reset an individual object or relationship.

## Serializing and deserializing complex types

By default, all data types are serialized and deserialized with `std::memcpy`. If this is not safe to do for a particular type, then properties storing that type must be defined as `type{object{𝘵𝘺𝘱𝘦 𝘯𝘢𝘮𝘦}}`. For every unique type that appears thusly in a property definition the data container will have the following function declarations:

- `uint64_t serialize_size(𝘵𝘺𝘱𝘦 𝘯𝘢𝘮𝘦 const&) const`
- `void serialize(std::byte*&, 𝘵𝘺𝘱𝘦 𝘯𝘢𝘮𝘦 const&) const`
- `void deserialize(std::byte const*&, 𝘵𝘺𝘱𝘦 𝘯𝘢𝘮𝘦& obj, std::byte const* end) const`

Each of these must have a user-provided definition. The first function must return a number of bytes greater than or equal to the number of bytes required to serialize the instance of the type passed as the parameter. The second function must write at most that many bytes starting at the pointer passed as the first parameter to serialize the instance passed as the second parameter. It must also advance the first parameter (which is a *reference* and thus also an out parameter) by the number of bytes written. Finally, the deserialize function must read bytes starting from the position passed as the first parameter to reconstruct the deserialized object, while the third parameter is the end of the provided data, and nothing should be read from that address or any byte after it. As with the `serialize` function, you must advance the first parameter by the number of bytes read by the `deserialize` function (which must be equal to the number written by the `serialize` function). When deserializing you may not make any assumptions about the object being deserialized into; it may or may not be in its default constructed state (but it will always be in a valid state).
			
## Deserializing an old record

legacy types
upgrading types

### Converting to different types

automatic and user defined

## Utility functions
