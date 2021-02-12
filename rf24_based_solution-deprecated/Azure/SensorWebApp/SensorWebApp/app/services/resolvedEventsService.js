angular.module('sensorWebApp')
    .factory('resolvedEventsDataFactory', ['$http', function ($http) {

        var urlBase = 'http://wilkiehomeautomation.azurewebsites.net/api/resolvedevent';
        var resolvedEventsDataFactory = {};

        resolvedEventsDataFactory.getResolvedEvents = function () {
            return $http.get(urlBase);
        };

        return resolvedEventsDataFactory;
    }]);