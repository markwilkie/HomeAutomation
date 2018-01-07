(function(i) {
    var array = i.split("&");
    var array2 = array[1].split("=");

    return array2[1];
})(input)