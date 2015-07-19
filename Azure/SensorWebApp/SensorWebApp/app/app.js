(function () {

    var app = angular.module('sensorWebApp', ['ngRoute', 'ngAnimate', 'ui.bootstrap', "mobile-angular-ui"]);

    app.config(['$routeProvider', function ($routeProvider) {
        var viewBase = '/app/views/';

        $routeProvider
            .when('/about', {
                controller: 'AboutController',
                templateUrl: viewBase + 'about.html',
                controllerAs: 'vm'
            })
            .when('/events', {
                controller: 'eventsController',
                templateUrl: viewBase + 'events.html',
                controllerAs: 'vm'
            })
            .when('/sensors', {
                controller: 'sensorsController',
                templateUrl: viewBase + 'sensors.html',
                controllerAs: 'vm'
            })
            .otherwise({ redirectTo: '/sensors' });

    }]);
}());

