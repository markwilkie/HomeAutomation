(function(i) {
    var array = i.split("&");
    if(array[1]=="code=C") {
        return "CLOSED";
    }
    else {
        return "OPEN";
    }
})(input)