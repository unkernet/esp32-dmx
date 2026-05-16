export function ccs(...list) {
    return list.filter(Boolean).join(' ');
}
