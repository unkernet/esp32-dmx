const TYPES = {
    u8: 1, u16: 2, u32: 4,
    i8: 1, i16: 2, i32: 4,
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/**
 * Calculates the size of a struct in bytes.
 * @param {any} struct Struct definition
 * @returns {number} Size in bytes
 */
export function sizeofStruct(struct) {
    if (typeof struct === 'string') {
        if (struct.startsWith('s')) return parseInt(struct.slice(1), 10);
        const size = TYPES[struct];
        if (!size) throw new TypeError('Unknown type: ' + struct);
        return size;
    }
    if (Array.isArray(struct)) {
        if (typeof struct[0] === 'number') {
            return struct[0] * sizeofStruct(struct[1]);
        }
        if (typeof struct[0] === 'string') {
            return sizeofStruct(struct[1]);
        }
        return struct.reduce((acc, field) => acc + sizeofStruct(field), 0);
    }
    return 0;
}

function decode(view, struct, state) {
    if (typeof struct === 'string') {
        const offset = state.offset;
        if (struct.startsWith('s')) {
            const len = parseInt(struct.slice(1), 10);
            const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, len);
            state.offset += len;
            const nullIndex = bytes.indexOf(0);
            const actualBytes = nullIndex === -1 ? bytes : bytes.subarray(0, nullIndex);
            return decoder.decode(actualBytes);
        }
        const size = TYPES[struct];
        if (!size) throw new TypeError('Unknown type: ' + struct);
        state.offset += size;
        switch (struct) {
            case 'u8': return view.getUint8(offset);
            case 'u16': return view.getUint16(offset, true);
            case 'u32': return view.getUint32(offset, true);
            case 'i8': return view.getInt8(offset);
            case 'i16': return view.getInt16(offset, true);
            case 'i32': return view.getInt32(offset, true);
        }
    }
    if (Array.isArray(struct)) {
        if (typeof struct[0] === 'number') {
            const res = [];
            for (let i = 0; i < struct[0]; i++) {
                res.push(decode(view, struct[1], state));
            }
            return res;
        }
        if (typeof struct[0] === 'string') {
            const val = decode(view, struct[1], state);
            if (struct[2] && Array.isArray(struct[2])) {
                const bits = {};
                struct[2].forEach((name, i) => {
                    if (name) bits[name] = !!(val & (1 << i));
                });
                return bits;
            }
            return val;
        }
        const obj = {};
        for (const field of struct) {
            obj[field[0]] = decode(view, field, state);
        }
        return obj;
    }
}

/**
 * Decodes an ArrayBuffer into an object based on a struct definition.
 * @param {ArrayBuffer|Uint8Array} data Data to decode
 * @param {any} struct Struct definition
 * @returns {any} Decoded object
 */
export function decodeStruct(data, struct) {
    const buffer = data.buffer || data;
    const byteOffset = data.byteOffset || 0;
    const view = new DataView(buffer, byteOffset);
    return decode(view, struct, { offset: 0 });
}

function encode(view, struct, data, state) {
    if (typeof struct === 'string') {
        const offset = state.offset;
        if (struct.startsWith('s')) {
            const len = parseInt(struct.slice(1), 10);
            const str = String(data || "");
            const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, len);
            bytes.fill(0);
            encoder.encodeInto(str, bytes);
            state.offset += len;
            return;
        }
        const size = TYPES[struct];
        if (!size) throw new TypeError('Unknown type: ' + struct);
        const val = data || 0;
        state.offset += size;
        switch (struct) {
            case 'u8': view.setUint8(offset, val); break;
            case 'u16': view.setUint16(offset, val, true); break;
            case 'u32': view.setUint32(offset, val, true); break;
            case 'i8': view.setInt8(offset, val); break;
            case 'i16': view.setInt16(offset, val, true); break;
            case 'i32': view.setInt32(offset, val, true); break;
        }
        return;
    }
    if (Array.isArray(struct)) {
        if (typeof struct[0] === 'number') {
            for (let i = 0; i < struct[0]; i++) {
                encode(view, struct[1], data ? data[i] : null, state);
            }
            return;
        }
        if (typeof struct[0] === 'string') {
            let val = data;
            if (struct[2] && Array.isArray(struct[2]) && typeof data === 'object' && data !== null) {
                val = 0;
                struct[2].forEach((name, i) => {
                    if (name && data[name]) val |= (1 << i);
                });
            }
            encode(view, struct[1], val, state);
            return;
        }
        for (const field of struct) {
            encode(view, field, data ? data[field[0]] : null, state);
        }
    }
}

/**
 * Encodes an object into an ArrayBuffer based on a struct definition.
 * @param {any} data Object to encode
 * @param {any} struct Struct definition
 * @returns {ArrayBuffer} Encoded buffer
 */
export function encodeStruct(data, struct) {
    const size = sizeofStruct(struct);
    const buffer = new ArrayBuffer(size);
    const view = new DataView(buffer);
    encode(view, struct, data, { offset: 0 });
    return buffer;
}
