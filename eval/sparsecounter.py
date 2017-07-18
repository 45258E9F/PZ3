import sys
from collections import defaultdict
import math

sys.path.append('/home/tsmart/dev/chengxi/z3/build/python/')
import z3


class Manager:
    """
    The manager class that stores the information on (in)equalities and constants.
    They are used to compute the sparseness and the constant factor.
    """
    def __init__(self):
        self._graph = defaultdict(set)
        self._function = defaultdict(set)
        self.clause_list = []
        self.edge_count = 0

    def add_equality(self, left, right):
        self._graph[left].add(right)
        self._graph[right].add(left)
        self.edge_count += 1

    def add_function_app(self, e):
        func_id = e.decl()
        app = FuncApp(e)
        self._function[func_id].add(app)

    def finalize_clause(self):
        self.clause_list.append(self.edge_count)
        self.edge_count = 0

    def reduce(self):
        for key in self._function:
            app_list = list(self._function[key])
            for i in range(0, len(app_list)):
                for j in range(0, i):
                    app_1 = app_list[i].args
                    app_2 = app_list[j].args
                    for k in range(0, len(app_1)):
                        id_1 = app_1[k]
                        id_2 = app_2[k]
                        self._graph[id_1].add(id_2)
                        self._graph[id_2].add(id_1)
                    id_1 = app_list[i].id
                    id_2 = app_list[j].id
                    self._graph[id_1].add(id_2)
                    self._graph[id_2].add(id_1)
                    self.clause_list.append(len(app_1) + 1)

    def get_visible_size(self):
        return sum([len(self._graph[i]) for i in self._graph]) / 2

    def get_var_size(self):
        return len(self._graph)

    def get_total_size(self):
        return sum(self.clause_list)

    def get_maximum_clause_size(self):
        return max(self.clause_list)


class FuncApp:
    """
    The class of function application.
    Note: we avoid to perform ackermann's reduction since the formula may be expanded exponentially
    """
    def __init__(self, e):
        self.app = e
        self.id = e.get_id()
        self.args = [ch.get_id() for ch in e.children()]

    def num_arg(self):
        return len(self.args)

    def __eq__(self, other):
        if type(self) is type(other):
            return self.id == other.id
        return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash(self.id)


def main(argv):
    smt_file = argv[0]
    z3.set_param('rewriter.div0_ackermann_limit', 1000000000)
    sparse, factor = calculate(smt_file)
    print(str(sparse) + "," + str(factor))


def calculate(input_smt):
    formula = z3.parse_smt2_file(input_smt)
    tactic_simp = z3.With(z3.Tactic('simplify'), 'elim_and', True)
    tactic_total = z3.Then(tactic_simp, z3.Tactic('elim-term-ite'))
    tactic_total = z3.Then(tactic_total, z3.Tactic('tseitin-cnf'))
    goals = tactic_total.apply(formula)
    if len(goals) == 1:
        goal = goals[0]
        # the goal is the list of constraints, and conjunction of which is equivalent to the original problem
        manager = compute(goal)
        # compute sparseness
        num_visible = manager.get_visible_size()
        num_var = manager.get_var_size()
        num_total = manager.get_total_size()
        num_min = manager.get_maximum_clause_size()
        sparse = (num_visible - num_min) / (num_total - num_min)
        factor_min = math.ceil((1 + math.sqrt(1 + 8 * num_visible)) / 2)
        factor_max = 2 * num_total
        factor = (num_var - factor_min) / (factor_max - factor_min)
        return sparse, factor
    else:
        return '*', '*'


def compute(goal_clauses):
    manager = Manager()
    for goal_clause in goal_clauses:
        for e in visitor_eq(goal_clause):
            children_list = e.children()
            if len(children_list) == 2:
                left = children_list[0]
                right = children_list[1]
                manager.add_equality(left.get_id(), right.get_id())
                # handle the function call
                for left_app in visitor_app(left):
                    manager.add_function_app(left_app)
                for right_app in visitor_app(right):
                    manager.add_function_app(right_app)
        manager.finalize_clause()
    manager.reduce()
    return manager


def visitor_eq(e):
    if z3.is_eq(e):
        yield e
    else:
        for ch in e.children():
            for e0 in visitor_eq(ch):
                yield e0


def visitor_app(e):
    if z3.is_app(e) and e.decl().kind() == z3.Z3_OP_UNINTERPRETED:
        if e.num_args() > 0:
            yield e
            for ch in e.children():
                for e0 in visitor_app(ch):
                    yield e0


if __name__ == "__main__":
    main(sys.argv[1:])
