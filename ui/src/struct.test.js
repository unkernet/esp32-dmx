import { decodeStruct, encodeStruct, sizeofStruct } from './struct.js';
import assert from 'assert';

function test() {
    console.log("Starting tests...");

    // 1. Basic struct
    const basicStruct = [
        ['id', 'u8'],
        ['val', 'u16']
    ];
    const basicData = { id: 10, val: 500 };
    const basicBuf = encodeStruct(basicData, basicStruct);
    assert.strictEqual(basicBuf.byteLength, 3);
    const decodedBasic = decodeStruct(basicBuf, basicStruct);
    assert.deepStrictEqual(decodedBasic, basicData);
    console.log("Basic struct test passed.");

    // 2. Dynamic array
    const dynamicStruct = [
        ['id', 'u8'],
        ['data', [-1, 'u8']]
    ];
    
    // Test decoding
    const dynamicBuf = new Uint8Array([5, 1, 2, 3, 4, 5]).buffer;
    const decodedDynamic = decodeStruct(dynamicBuf, dynamicStruct);
    assert.deepStrictEqual(decodedDynamic, { id: 5, data: [1, 2, 3, 4, 5] });
    console.log("Dynamic array decoding test passed.");

    // Test sizeofStruct for dynamic array (should assume 1 element)
    const size = sizeofStruct(dynamicStruct);
    assert.strictEqual(size, 1 + 1); // u8 + 1*u8
    console.log("sizeofStruct dynamic array test passed.");

    // Test encoding dynamic array
    const dataMulti = { id: 5, data: [10, 20, 30, 40, 50] };
    const bufMulti = encodeStruct(dataMulti, dynamicStruct);
    assert.strictEqual(bufMulti.byteLength, 1 + 5);
    assert.deepStrictEqual(decodeStruct(bufMulti, dynamicStruct), dataMulti);
    console.log("Dynamic array encoding (multi element) passed.");

    // 4. Complex nested struct with UTF-8 and signed/unsigned types
    const complexStruct = [
        ['header', 'u32'],
        ['signed8', 'i8'],
        ['signed16', 'i16'],
        ['signed32', 'i32'],
        ['nested', [
            ['msg', 's20'],
            ['flags', 'u8', ['ok', 'err']]
        ]],
        ['scores', [2, 'i16']],
        ['tags', [-1, 's6']] // Dynamic array of 6-byte strings
    ];

    const complexData = {
        header: 0xDEADBEEF,
        signed8: -128,
        signed16: -32768,
        signed32: -2147483648,
        nested: {
            msg: "สวัสดี", // Thai "Hello"
            flags: { ok: true, err: true }
        },
        scores: [-100, 25000],
        tags: ["tag1", "Ö", "😊"]
    };

    console.log("Testing complex nested struct...");
    const complexBuf = encodeStruct(complexData, complexStruct);
    const decodedComplex = decodeStruct(complexBuf, complexStruct);

    // Verify integers
    assert.strictEqual(decodedComplex.header, 0xDEADBEEF >>> 0); // Convert to unsigned for comparison
    assert.strictEqual(decodedComplex.signed8, -128);
    assert.strictEqual(decodedComplex.signed16, -32768);
    assert.strictEqual(decodedComplex.signed32, -2147483648);

    // Verify nested and UTF-8
    assert.strictEqual(decodedComplex.nested.msg, "สวัสดี");
    assert.deepStrictEqual(decodedComplex.nested.flags, { ok: true, err: true });

    // Verify arrays
    assert.deepStrictEqual(decodedComplex.scores, [-100, 25000]);
    assert.deepStrictEqual(decodedComplex.tags, ["tag1", "Ö", "😊"]);

    console.log("Complex nested struct test passed.");

    console.log("All tests passed!");
}

test();
