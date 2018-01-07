function pad(n) {return n < 10 ? "0"+n : n;}

(function(i) {
    var array = i.split("&");
    var array2 = array[2].split("=");

    var parsedDate = new Date(array2[1] * 1000);

    var humanDate = parsedDate.getMonth() + '/' + parsedDate.getDate() + ' ' + parsedDate.getHours() + ':' + pad(parsedDate.getMinutes());
    return humanDate;
    //return new Date(array2[1] * 1000).toString();
})(input)